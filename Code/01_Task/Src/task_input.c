/**
 * @file    task_input.c
 * @brief   Input task.
 */

#include "task_input.h"

#include "cmsis_os.h"
#include "task_init.h"

#include "mod_key.h"

#include "task_chassis.h"
#include "task_debug.h"

#define TASK_INPUT_PERIOD_MS (10U)

typedef enum
{
    TASK_INPUT_STATE_IDLE = 0,
    TASK_INPUT_STATE_LAP_SELECT,
} task_input_state_t;

static volatile bool s_lap_select_active = false;
static volatile uint8_t s_selected_laps = 0U;

static void task_input_enter_line_track(uint8_t lap_target)
{
    (void)task_debug_set_mode(TASK_DEBUG_MODE_NONE);
    (void)task_chassis_set_line_track_laps(g_task_chassis_pid.line_follow_base_speed,
                                           g_task_chassis_pid.line_target,
                                           lap_target);
}

bool task_input_is_lap_select_active(void)
{
    return s_lap_select_active;
}

uint8_t task_input_get_selected_laps(void)
{
    return s_selected_laps;
}

static void task_input_enter_vofa_debug(void)
{
    (void)task_chassis_set_speed_debug_target(g_task_chassis_pid.debug_target_left,
                                              g_task_chassis_pid.debug_target_right);
    (void)task_debug_set_mode(TASK_DEBUG_MODE_PID_TUNE_UART);
}

static void task_input_enter_pwm_debug(void)
{
    (void)task_chassis_set_pwm_debug_duty(g_task_chassis_pid.pwm_debug_left_duty,
                                          g_task_chassis_pid.pwm_debug_right_duty);
    (void)task_debug_set_mode(TASK_DEBUG_MODE_PID_TUNE_UART);
}

void StartInputTask(void *argument)
{
    mod_key_ctx_t *key_ctx;
    mod_key_event_e key_event;
    task_input_state_t input_state = TASK_INPUT_STATE_IDLE;
    uint8_t selected_laps = 0U;

    (void)argument;

    task_wait_init_done();
    key_ctx = mod_key_get_default_ctx();

    for (;;)
    {
        key_event = mod_key_scan(key_ctx);

        if (key_event == MOD_KEY_EVENT_1_CLICK)
        {
            (void)task_debug_set_mode(TASK_DEBUG_MODE_NONE);
            (void)task_chassis_set_mode(TASK_CHASSIS_CMD_MODE_IDLE);
            input_state = TASK_INPUT_STATE_LAP_SELECT;
            selected_laps = 0U;
            s_lap_select_active = true;
            s_selected_laps = selected_laps;
        }
        else if (key_event == MOD_KEY_EVENT_1_LONG_PRESS)
        {
            input_state = TASK_INPUT_STATE_IDLE;
            s_lap_select_active = false;
            s_selected_laps = 0U;
            task_input_enter_vofa_debug();
        }
        else if (key_event == MOD_KEY_EVENT_2_CLICK)
        {
            input_state = TASK_INPUT_STATE_IDLE;
            s_lap_select_active = false;
            s_selected_laps = 0U;
            task_input_enter_pwm_debug();
        }
        else if ((input_state == TASK_INPUT_STATE_LAP_SELECT) &&
                 (key_event == MOD_KEY_EVENT_3_CLICK))
        {
            if (selected_laps < 255U)
            {
                selected_laps++;
            }

            s_selected_laps = selected_laps;
        }
        else if ((input_state == TASK_INPUT_STATE_LAP_SELECT) &&
                 (key_event == MOD_KEY_EVENT_3_LONG_PRESS))
        {
            if (selected_laps == 0U)
            {
                selected_laps = 1U;
            }

            task_input_enter_line_track(selected_laps);
            input_state = TASK_INPUT_STATE_IDLE;
            s_lap_select_active = false;
            s_selected_laps = 0U;
            selected_laps = 0U;
        }

        osDelay(TASK_INPUT_PERIOD_MS);
    }
}
