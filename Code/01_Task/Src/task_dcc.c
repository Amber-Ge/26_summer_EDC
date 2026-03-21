#include "task_dcc.h"
#include "task_init.h"
#include <stddef.h>

#define TASK_DCC_GRAY_STOP_COUNT (3U)
#define TASK_DCC_STARTUP_DELAY_MS (3000U)

static volatile uint8_t s_dcc_mode = TASK_DCC_MODE_IDLE;
static volatile uint8_t s_dcc_run_state = TASK_DCC_RUN_OFF;

static float clamp_float(float value, float min, float max)
{
    float result = value;
    if (result < min)
    {
        result = min;
    }
    if (result > max)
    {
        result = max;
    }
    return result;
}

static int16_t convert_to_duty_cmd(float duty_f)
{
    float duty_limited;

    duty_limited = clamp_float(duty_f, -(float)MOD_MOTOR_DUTY_MAX, (float)MOD_MOTOR_DUTY_MAX);
    if (duty_limited >= 0.0f)
    {
        duty_limited += 0.5f;
    }
    else
    {
        duty_limited -= 0.5f;
    }

    return (int16_t)duty_limited;
}

static void dcc_stop_motor_now(void)
{
    mod_motor_set_duty(MOD_MOTOR_LEFT, 0);
    mod_motor_set_duty(MOD_MOTOR_RIGHT, 0);
}

static void dcc_reset_pid(pid_pos_t *pos_pid, pid_inc_t *left_speed_pid, pid_inc_t *right_speed_pid)
{
    if ((pos_pid == NULL) || (left_speed_pid == NULL) || (right_speed_pid == NULL))
    {
        return;
    }

    PID_Pos_Reset(pos_pid);
    PID_Inc_Reset(left_speed_pid);
    PID_Inc_Reset(right_speed_pid);
    PID_Pos_SetTarget(pos_pid, MOTOR_TARGET_ERROR);
}

static uint32_t dcc_ms_to_ticks(uint32_t duration_ms, uint32_t tick_freq)
{
    uint32_t ticks;

    if (tick_freq == 0U)
    {
        tick_freq = 1000U;
    }

    ticks = (uint32_t)(((uint64_t)duration_ms * (uint64_t)tick_freq + 999ULL) / 1000ULL);
    if (ticks == 0U)
    {
        ticks = 1U;
    }
    return ticks;
}

static void dcc_enter_off(pid_pos_t *pos_pid,
                          pid_inc_t *left_speed_pid,
                          pid_inc_t *right_speed_pid,
                          uint8_t *gray_trigger_streak)
{
    s_dcc_run_state = TASK_DCC_RUN_OFF;
    if (gray_trigger_streak != NULL)
    {
        *gray_trigger_streak = 0U;
    }
    dcc_stop_motor_now();
    dcc_reset_pid(pos_pid, left_speed_pid, right_speed_pid);
}

uint8_t task_dcc_get_mode(void)
{
    return s_dcc_mode;
}

uint8_t task_dcc_get_run_state(void)
{
    return s_dcc_run_state;
}

uint8_t task_dcc_get_ready(void)
{
    return (uint8_t)(s_dcc_run_state == TASK_DCC_RUN_ON);
}

void StartDccTask(void *argument)
{
    pid_pos_t pos_pid;
    pid_inc_t left_speed_pid;
    pid_inc_t right_speed_pid;
    uint8_t gray_trigger_streak = 0U;
    uint32_t tick_freq;
    uint32_t prepare_duration_tick;
    uint32_t prepare_start_tick = 0U;

    (void)argument;

    task_wait_init_done();

    PID_Pos_Init(&pos_pid,
                 MOTOR_POS_KP,
                 MOTOR_POS_KI,
                 MOTOR_POS_KD,
                 MOTOR_POS_OUTPUT_MAX,
                 MOTOR_POS_INTEGRAL_MAX);
    PID_Pos_SetTarget(&pos_pid, MOTOR_TARGET_ERROR);

    PID_Inc_Init(&left_speed_pid,
                 MOTOR_SPEED_KP,
                 MOTOR_SPEED_KI,
                 MOTOR_SPEED_KD,
                 (float)MOD_MOTOR_DUTY_MAX);

    PID_Inc_Init(&right_speed_pid,
                 MOTOR_SPEED_KP,
                 MOTOR_SPEED_KI,
                 MOTOR_SPEED_KD,
                 (float)MOD_MOTOR_DUTY_MAX);

    tick_freq = osKernelGetTickFreq();
    prepare_duration_tick = dcc_ms_to_ticks(TASK_DCC_PREPARE_MS, tick_freq);

    osDelay(TASK_DCC_STARTUP_DELAY_MS);
    dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);

    for (;;)
    {
        if (osSemaphoreAcquire(Sem_TaskChangeHandle, 0U) == osOK)
        {
            s_dcc_mode = (uint8_t)((s_dcc_mode + 1U) % TASK_DCC_MODE_TOTAL);
            dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);
        }

        if (osSemaphoreAcquire(Sem_ReadyToggleHandle, 0U) == osOK)
        {
            if (s_dcc_run_state == TASK_DCC_RUN_OFF)
            {
                s_dcc_run_state = TASK_DCC_RUN_PREPARE;
                prepare_start_tick = osKernelGetTickCount();
                gray_trigger_streak = 0U;
                dcc_stop_motor_now();
                dcc_reset_pid(&pos_pid, &left_speed_pid, &right_speed_pid);
            }
            else
            {
                dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);
            }
        }

        if (s_dcc_run_state == TASK_DCC_RUN_PREPARE)
        {
            uint32_t now_tick = osKernelGetTickCount();
            if ((uint32_t)(now_tick - prepare_start_tick) >= prepare_duration_tick)
            {
                s_dcc_run_state = TASK_DCC_RUN_ON;
                gray_trigger_streak = 0U;
                dcc_stop_motor_now();
                dcc_reset_pid(&pos_pid, &left_speed_pid, &right_speed_pid);
            }
        }

        mod_motor_tick();

        if (s_dcc_run_state != TASK_DCC_RUN_ON)
        {
            dcc_stop_motor_now();
            osDelay(TASK_DCC_PERIOD_MS);
            continue;
        }

        if (s_dcc_mode == TASK_DCC_MODE_IDLE)
        {
            gray_trigger_streak = 0U;
            dcc_stop_motor_now();
        }
        else if (s_dcc_mode == TASK_DCC_MODE_STRAIGHT)
        {
            int64_t left_pos;
            int64_t right_pos;
            float pos_error;
            float outer_output;
            float left_target_speed;
            float right_target_speed;
            float left_feedback_speed;
            float right_feedback_speed;
            float left_duty_f;
            float right_duty_f;
            /* uint16_t sensor_raw; */

            /* 灰度急停逻辑暂时屏蔽（仅注释，不删除） */
            /*
            sensor_raw = mod_sensor_get_raw_data();
            if (sensor_raw != 0U)
            {
                if (gray_trigger_streak < 255U)
                {
                    gray_trigger_streak++;
                }
            }
            else
            {
                gray_trigger_streak = 0U;
            }

            if (gray_trigger_streak >= TASK_DCC_GRAY_STOP_COUNT)
            {
                dcc_stop_motor_now();
                dcc_reset_pid(&pos_pid, &left_speed_pid, &right_speed_pid);
                s_dcc_mode = TASK_DCC_MODE_IDLE;
                gray_trigger_streak = 0U;
                osDelay(TASK_DCC_PERIOD_MS);
                continue;
            }
            */

            left_pos = mod_motor_get_position(MOD_MOTOR_LEFT);
            right_pos = mod_motor_get_position(MOD_MOTOR_RIGHT);
            pos_error = (float)(left_pos - right_pos);

            outer_output = PID_Pos_Compute(&pos_pid, -pos_error);
            outer_output = clamp_float(outer_output, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

            left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - outer_output);
            right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + outer_output);

            left_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_LEFT);
            right_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_RIGHT);

            PID_Inc_SetTarget(&left_speed_pid, left_target_speed);
            PID_Inc_SetTarget(&right_speed_pid, right_target_speed);
            left_duty_f = PID_Inc_Compute(&left_speed_pid, left_feedback_speed);
            right_duty_f = PID_Inc_Compute(&right_speed_pid, right_feedback_speed);

            mod_motor_set_duty(MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
            mod_motor_set_duty(MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));
        }
        else
        {
            gray_trigger_streak = 0U;
            dcc_stop_motor_now();
        }

        osDelay(TASK_DCC_PERIOD_MS);
    }
}
