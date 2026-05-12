/**
 * @file    task_chassis.c
 * @brief   Chassis control task.
 */

#include "task_chassis.h"

#include <string.h>

#include "cmsis_os.h"
#include "main.h"
#include "tim.h"
#include "task_init.h"

#include "mod_motor.h"
#include "mod_sensor.h"
#include "pid_inc.h"
#include "pid_pos.h"

#define TASK_CHASSIS_PERIOD_MS (10U)
/*方向修正系数*/
#define TASK_CHASSIS_LEFT_OUTPUT_SIGN   (-1)
#define TASK_CHASSIS_RIGHT_OUTPUT_SIGN  (1)
#define TASK_CHASSIS_LEFT_FEEDBACK_SIGN (-1)
#define TASK_CHASSIS_RIGHT_FEEDBACK_SIGN (-1)

typedef enum
{
    TASK_CHASSIS_CORNER_NONE = 0,
    TASK_CHASSIS_CORNER_LEFT = -1,
    TASK_CHASSIS_CORNER_RIGHT = 1,
} task_chassis_corner_state_t;
/*控制参数*/
const task_chassis_pid_group_t g_task_chassis_pid = {
    .line_follow = {
        .kp = 45.0f,
        .output_max = 500.0f,
    },
    .corner = {
        .turn_speed = 30,
        .enter_ticks = 1,
        .turn_hold_ticks = 8,
        .exit_ticks = 3,
    },
    .left_speed_inner = {
        .kp = 23.0f,
        .ki = 3.0f,
        .kd = 0.0f,
        .output_max = 1000.0f,
        .integral_max = 200.0f,
    },
    .right_speed_inner = {
        .kp = 23.0f,
        .ki = 3.0f,
        .kd = 0.0f,
        .output_max = 1000.0f,
        .integral_max = 200.0f,
    },
    .debug_target_left = 50,
    .debug_target_right = 50,
    .pwm_debug_left_duty = 800,
    .pwm_debug_right_duty = 800,
    .line_follow_base_speed = 25,
    .target_speed_step_max = 20,
    .line_target = 0.0f,
};

static task_chassis_cmd_t s_test_cmd = {
    TASK_CHASSIS_CMD_MODE_IDLE,
    0,
    0,
    0,
    0.0f,
    0U
};
static task_chassis_telemetry_t s_telemetry = {
    false, TASK_CHASSIS_CMD_MODE_IDLE, false, false,
    0, 0, 0, 0,
    0, 0,
    0, 0,
    0, 0, 0, 0, 0, 0,
    {0},
    0.0f,
    0.0f
};

/* 进入临界区，保护任务共享命令和遥测数据。 */
static uint32_t task_chassis_critical_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/* 退出临界区，恢复进入前的中断状态。 */
static void task_chassis_critical_exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/* 将浮点数限制在给定区间内。 */

static float task_chassis_clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

/* 将浮点占空比限制并四舍五入为整数占空比命令。 */
static int16_t task_chassis_float_to_duty(float duty_f)
{
    float duty_limited = task_chassis_clamp_float(
        duty_f,
        -(float)MOD_MOTOR_DUTY_MAX,
        (float)MOD_MOTOR_DUTY_MAX);

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

/* 将 32 位整数限制在给定区间内。 */
static int32_t task_chassis_clamp_int32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }

    return value;
}

/* 对目标速度施加斜坡限制，避免相邻周期跳变过大。 */
static int32_t task_chassis_apply_target_slew(int32_t target,
                                              int32_t last_target,
                                              int32_t step_max)
{
    int32_t delta;

    if (step_max <= 0)
    {
        return target;
    }

    delta = target - last_target;
    delta = task_chassis_clamp_int32(delta, -step_max, step_max);
    return last_target + delta;
}

/* 按参数表初始化速度环 PID。 */
static void task_chassis_init_speed_pid(pid_pos_t *pid, const task_chassis_pid_param_t *param)
{
    if ((pid == NULL) || (param == NULL))
    {
        return;
    }

    PID_Pos_Init(pid,
                 param->kp,
                 param->ki,
                 param->kd,
                 param->output_max,
                 param->integral_max);
}

/* 根据电机安装方向修正速度反馈符号。 */
static int32_t task_chassis_apply_feedback_sign(mod_motor_id_e id, int32_t value)
{
    if (id == MOD_MOTOR_LEFT)
    {
        return value * TASK_CHASSIS_LEFT_FEEDBACK_SIGN;
    }

    return value * TASK_CHASSIS_RIGHT_FEEDBACK_SIGN;
}

/* 根据电机安装方向修正位置反馈符号。 */
static int64_t task_chassis_apply_feedback_sign_i64(mod_motor_id_e id, int64_t value)
{
    if (id == MOD_MOTOR_LEFT)
    {
        return value * TASK_CHASSIS_LEFT_FEEDBACK_SIGN;
    }

    return value * TASK_CHASSIS_RIGHT_FEEDBACK_SIGN;
}

/* 根据电机安装方向修正输出占空比符号。 */
static int16_t task_chassis_apply_output_sign(mod_motor_id_e id, int16_t duty)
{
    if (id == MOD_MOTOR_LEFT)
    {
        return (int16_t)(duty * TASK_CHASSIS_LEFT_OUTPUT_SIGN);
    }

    return (int16_t)(duty * TASK_CHASSIS_RIGHT_OUTPUT_SIGN);
}

/* 根据 8 路循线传感器状态识别当前是否进入左/右拐角。 */
static task_chassis_corner_state_t task_chassis_detect_corner_pattern(const uint8_t *states)
{
    bool left_four_on;
    bool right_four_on;
    bool left_three_off;
    bool right_three_off;
    bool left_outer_two_on;
    bool right_outer_two_on;

    if (states == NULL)
    {
        return TASK_CHASSIS_CORNER_NONE;
    }

    left_four_on = (states[0] != 0U) &&
                   (states[1] != 0U) &&
                   (states[2] != 0U) &&
                   (states[3] != 0U);
    right_four_on = (states[4] != 0U) &&
                    (states[5] != 0U) &&
                    (states[6] != 0U) &&
                    (states[7] != 0U);
    left_three_off = (states[0] == 0U) &&
                     (states[1] == 0U) &&
                     (states[2] == 0U);
    right_three_off = (states[5] == 0U) &&
                      (states[6] == 0U) &&
                      (states[7] == 0U);
    left_outer_two_on = (states[0] != 0U) &&
                        (states[1] != 0U);
    right_outer_two_on = (states[6] != 0U) &&
                         (states[7] != 0U);

    if ((left_four_on && right_three_off) || left_outer_two_on)
    {
        return TASK_CHASSIS_CORNER_LEFT;
    }

    if ((right_four_on && left_three_off) || right_outer_two_on)
    {
        return TASK_CHASSIS_CORNER_RIGHT;
    }

    return TASK_CHASSIS_CORNER_NONE;
}

/* 判断 8 路循线传感器是否全部丢线。 */
static bool task_chassis_is_all_sensors_off(const uint8_t *states)
{
    if (states == NULL)
    {
        return false;
    }

    for (uint8_t i = 0U; i < 8U; i++)
    {
        if (states[i] != 0U)
        {
            return false;
        }
    }

    return true;
}

/* 发布遥测快照，供其他任务安全读取。 */
static void task_chassis_publish_telemetry(const task_chassis_telemetry_t *telemetry)
{
    uint32_t primask;

    if (telemetry == NULL)
    {
        return;
    }

    primask = task_chassis_critical_enter();
    s_telemetry = *telemetry;
    task_chassis_critical_exit(primask);
}

/* 原子更新底盘控制命令。 */
bool task_chassis_set_command(const task_chassis_cmd_t *cmd)
{
    uint32_t primask;

    if (cmd == NULL)
    {
        return false;
    }

    primask = task_chassis_critical_enter();
    s_test_cmd = *cmd;
    task_chassis_critical_exit(primask);
    return true;
}

/* 切换底盘模式，并填充该模式需要的默认命令参数。 */
bool task_chassis_set_mode(task_chassis_cmd_mode_t mode)
{
    task_chassis_cmd_t cmd;
    uint32_t primask;

    if (mode >= TASK_CHASSIS_CMD_MODE_LINE_TRACK + 1)
    {
        return false;
    }

    primask = task_chassis_critical_enter();
    cmd = s_test_cmd;
    task_chassis_critical_exit(primask);

    cmd.mode = mode;

    if (mode == TASK_CHASSIS_CMD_MODE_IDLE)
    {
        cmd.left_target_speed = 0;
        cmd.right_target_speed = 0;
        cmd.lap_target = 0U;
    }
    else if (mode == TASK_CHASSIS_CMD_MODE_LINE_TRACK)
    {
        cmd.base_speed = g_task_chassis_pid.line_follow_base_speed;
        cmd.line_target = g_task_chassis_pid.line_target;
        cmd.lap_target = 0U;
    }
    else
    {
        cmd.lap_target = 0U;
    }

    return task_chassis_set_command(&cmd);
}

/* 配置速度闭环调试命令。 */
bool task_chassis_set_speed_debug_target(int32_t left_target_speed, int32_t right_target_speed)
{
    task_chassis_cmd_t cmd;

    cmd.mode = TASK_CHASSIS_CMD_MODE_SPEED_DEBUG;
    cmd.left_target_speed = left_target_speed;
    cmd.right_target_speed = right_target_speed;
    cmd.base_speed = 0;
    cmd.line_target = g_task_chassis_pid.line_target;
    cmd.lap_target = 0U;

    return task_chassis_set_command(&cmd);
}

/* 配置 PWM 开环调试命令。 */
bool task_chassis_set_pwm_debug_duty(int16_t left_duty, int16_t right_duty)
{
    task_chassis_cmd_t cmd;

    cmd.mode = TASK_CHASSIS_CMD_MODE_PWM_DEBUG;
    cmd.left_target_speed = left_duty;
    cmd.right_target_speed = right_duty;
    cmd.base_speed = 0;
    cmd.line_target = g_task_chassis_pid.line_target;
    cmd.lap_target = 0U;

    return task_chassis_set_command(&cmd);
}

/* 配置循线模式参数，不限制圈数。 */
bool task_chassis_set_line_track_target(int32_t base_speed, float line_target)
{
    return task_chassis_set_line_track_laps(base_speed, line_target, 0U);
}

/* 配置循线模式参数，并可选设置自动停车圈数。 */
bool task_chassis_set_line_track_laps(int32_t base_speed, float line_target, uint8_t lap_target)
{
    task_chassis_cmd_t cmd;

    cmd.mode = TASK_CHASSIS_CMD_MODE_LINE_TRACK;
    cmd.left_target_speed = 0;
    cmd.right_target_speed = 0;
    cmd.base_speed = base_speed;
    cmd.line_target = line_target;
    cmd.lap_target = lap_target;

    return task_chassis_set_command(&cmd);
}

/* 读取最近一次发布的遥测快照。 */
bool task_chassis_get_telemetry(task_chassis_telemetry_t *telemetry)
{
    uint32_t primask;

    if (telemetry == NULL)
    {
        return false;
    }

    primask = task_chassis_critical_enter();
    *telemetry = s_telemetry;
    task_chassis_critical_exit(primask);
    return true;
}
/**************************************************************************/
/* 底盘主任务：周期采样、模式分发、速度控制和遥测发布。 */
void StartChassisTask(void *argument)
{
    mod_motor_ctx_t *motor_ctx = mod_motor_get_default_ctx();
    mod_sensor_ctx_t *sensor_ctx = mod_sensor_get_default_ctx();
    pid_pos_t left_speed_pid;
    pid_pos_t right_speed_pid;
    task_chassis_cmd_t cmd_local;
    task_chassis_telemetry_t telemetry_local;
    task_chassis_cmd_mode_t last_mode = TASK_CHASSIS_CMD_MODE_IDLE;
    task_chassis_corner_state_t corner_state = TASK_CHASSIS_CORNER_NONE;
    task_chassis_corner_state_t corner_candidate = TASK_CHASSIS_CORNER_NONE;
    int32_t last_left_target_speed = 0;
    int32_t last_right_target_speed = 0;
    uint8_t corner_enter_count = 0U;
    uint8_t corner_hold_count = 0U;
    uint8_t corner_exit_count = 0U;
    uint16_t corner_pass_count = 0U;

    (void)argument;

    task_wait_init_done();

    task_chassis_init_speed_pid(&left_speed_pid, &g_task_chassis_pid.left_speed_inner);
    task_chassis_init_speed_pid(&right_speed_pid, &g_task_chassis_pid.right_speed_inner);

    mod_motor_set_mode(motor_ctx, MOD_MOTOR_LEFT, MOTOR_MODE_DRIVE);
    mod_motor_set_mode(motor_ctx, MOD_MOTOR_RIGHT, MOTOR_MODE_DRIVE);
    mod_motor_set_duty(motor_ctx, MOD_MOTOR_LEFT, 0);
    mod_motor_set_duty(motor_ctx, MOD_MOTOR_RIGHT, 0);

    for (;;)
    {
        uint32_t primask;
        int32_t left_actual_speed;
        int32_t right_actual_speed;
        int64_t left_total_position;
        int64_t right_total_position;
        bool left_encoder_ready;
        bool right_encoder_ready;
        int32_t left_encoder_counter_raw;
        int32_t right_encoder_counter_raw;
        uint8_t left_encoder_a_level;
        uint8_t left_encoder_b_level;
        uint8_t right_encoder_a_level;
        uint8_t right_encoder_b_level;
        int16_t left_duty_cmd = 0;
        int16_t right_duty_cmd = 0;
        uint8_t line_sensor_states[8] = {0};
        float line_weight = 0.0f;
        float line_turn_output = 0.0f;

        mod_motor_tick(motor_ctx);
        left_actual_speed = task_chassis_apply_feedback_sign(
            MOD_MOTOR_LEFT,
            mod_motor_get_speed(motor_ctx, MOD_MOTOR_LEFT));
        right_actual_speed = task_chassis_apply_feedback_sign(
            MOD_MOTOR_RIGHT,
            mod_motor_get_speed(motor_ctx, MOD_MOTOR_RIGHT));
        left_total_position = task_chassis_apply_feedback_sign_i64(
            MOD_MOTOR_LEFT,
            mod_motor_get_position(motor_ctx, MOD_MOTOR_LEFT));
        right_total_position = task_chassis_apply_feedback_sign_i64(
            MOD_MOTOR_RIGHT,
            mod_motor_get_position(motor_ctx, MOD_MOTOR_RIGHT));
        left_encoder_ready = mod_motor_is_encoder_ready(motor_ctx, MOD_MOTOR_LEFT);
        right_encoder_ready = mod_motor_is_encoder_ready(motor_ctx, MOD_MOTOR_RIGHT);

        left_encoder_counter_raw = task_chassis_apply_feedback_sign(
            MOD_MOTOR_LEFT,
            mod_motor_get_encoder_counter_raw(motor_ctx, MOD_MOTOR_LEFT));
        right_encoder_counter_raw = task_chassis_apply_feedback_sign(
            MOD_MOTOR_RIGHT,
            mod_motor_get_encoder_counter_raw(motor_ctx, MOD_MOTOR_RIGHT));

        left_encoder_a_level = (uint8_t)HAL_GPIO_ReadPin(L_A1_GPIO_Port, L_A1_Pin);
        left_encoder_b_level = (uint8_t)HAL_GPIO_ReadPin(L_A2_GPIO_Port, L_A2_Pin);
        right_encoder_a_level = (uint8_t)HAL_GPIO_ReadPin(R_B1_GPIO_Port, R_B1_Pin);
        right_encoder_b_level = (uint8_t)HAL_GPIO_ReadPin(R_B2_GPIO_Port, R_B2_Pin);

        primask = task_chassis_critical_enter();
        cmd_local = s_test_cmd;
        task_chassis_critical_exit(primask);

        (void)mod_sensor_get_states(sensor_ctx, line_sensor_states, (uint8_t)sizeof(line_sensor_states));
        line_weight = mod_sensor_get_weight(sensor_ctx);

        if (cmd_local.mode == TASK_CHASSIS_CMD_MODE_SPEED_DEBUG)
        {
            if (last_mode != cmd_local.mode)
            {
                PID_Pos_Reset(&left_speed_pid);
                PID_Pos_Reset(&right_speed_pid);
            }

            PID_Pos_SetTarget(&left_speed_pid, (float)cmd_local.left_target_speed);
            PID_Pos_SetTarget(&right_speed_pid, (float)cmd_local.right_target_speed);

            left_duty_cmd = task_chassis_float_to_duty(
                PID_Pos_Compute(&left_speed_pid, (float)left_actual_speed));
            right_duty_cmd = task_chassis_float_to_duty(
                PID_Pos_Compute(&right_speed_pid, (float)right_actual_speed));

            mod_motor_set_duty(motor_ctx,
                               MOD_MOTOR_LEFT,
                               task_chassis_apply_output_sign(MOD_MOTOR_LEFT, left_duty_cmd));
            mod_motor_set_duty(motor_ctx,
                               MOD_MOTOR_RIGHT,
                               task_chassis_apply_output_sign(MOD_MOTOR_RIGHT, right_duty_cmd));
        }
        else if (cmd_local.mode == TASK_CHASSIS_CMD_MODE_PWM_DEBUG)
        {
            if (last_mode != cmd_local.mode)
            {
                PID_Pos_Reset(&left_speed_pid);
                PID_Pos_Reset(&right_speed_pid);
            }

            left_duty_cmd = task_chassis_clamp_int32(cmd_local.left_target_speed,
                                                     -(int32_t)MOD_MOTOR_DUTY_MAX,
                                                     (int32_t)MOD_MOTOR_DUTY_MAX);
            right_duty_cmd = task_chassis_clamp_int32(cmd_local.right_target_speed,
                                                      -(int32_t)MOD_MOTOR_DUTY_MAX,
                                                      (int32_t)MOD_MOTOR_DUTY_MAX);

            mod_motor_set_duty(motor_ctx,
                               MOD_MOTOR_LEFT,
                               task_chassis_apply_output_sign(MOD_MOTOR_LEFT, left_duty_cmd));
            mod_motor_set_duty(motor_ctx,
                               MOD_MOTOR_RIGHT,
                               task_chassis_apply_output_sign(MOD_MOTOR_RIGHT, right_duty_cmd));
        }
        else if (cmd_local.mode == TASK_CHASSIS_CMD_MODE_LINE_TRACK)
        {
            int32_t left_target_speed_raw;
            int32_t right_target_speed_raw;
            float line_error;
            task_chassis_corner_state_t detected_corner;
            bool all_sensors_off;

            if (last_mode != cmd_local.mode)
            {
                PID_Pos_Reset(&left_speed_pid);
                PID_Pos_Reset(&right_speed_pid);
                corner_state = TASK_CHASSIS_CORNER_NONE;
                corner_candidate = TASK_CHASSIS_CORNER_NONE;
                corner_enter_count = 0U;
                corner_hold_count = 0U;
                corner_exit_count = 0U;
                corner_pass_count = 0U;
                last_left_target_speed = 0;
                last_right_target_speed = 0;
            }

            if ((cmd_local.lap_target > 0U) &&
                (corner_pass_count >= ((uint16_t)cmd_local.lap_target * 4U)))
            {
                task_chassis_cmd_t stop_cmd;

                stop_cmd = cmd_local;
                stop_cmd.mode = TASK_CHASSIS_CMD_MODE_IDLE;
                stop_cmd.left_target_speed = 0;
                stop_cmd.right_target_speed = 0;
                stop_cmd.base_speed = 0;
                stop_cmd.line_target = g_task_chassis_pid.line_target;
                stop_cmd.lap_target = 0U;
                (void)task_chassis_set_command(&stop_cmd);

                PID_Pos_Reset(&left_speed_pid);
                PID_Pos_Reset(&right_speed_pid);
                corner_state = TASK_CHASSIS_CORNER_NONE;
                corner_candidate = TASK_CHASSIS_CORNER_NONE;
                corner_enter_count = 0U;
                corner_hold_count = 0U;
                corner_exit_count = 0U;
                corner_pass_count = 0U;
                last_left_target_speed = 0;
                last_right_target_speed = 0;
                cmd_local = stop_cmd;
                left_duty_cmd = 0;
                right_duty_cmd = 0;
                mod_motor_set_duty(motor_ctx, MOD_MOTOR_LEFT, 0);
                mod_motor_set_duty(motor_ctx, MOD_MOTOR_RIGHT, 0);
            }
            else
            {

            detected_corner = task_chassis_detect_corner_pattern(line_sensor_states);
            all_sensors_off = task_chassis_is_all_sensors_off(line_sensor_states);

            if (corner_state == TASK_CHASSIS_CORNER_NONE)
            {
                corner_exit_count = 0U;

                if (detected_corner != TASK_CHASSIS_CORNER_NONE)
                {
                    if (corner_candidate != detected_corner)
                    {
                        corner_candidate = detected_corner;
                        corner_enter_count = 1U;
                    }
                    else if (corner_enter_count < 255U)
                    {
                        corner_enter_count++;
                    }

                    if (corner_enter_count >= g_task_chassis_pid.corner.enter_ticks)
                    {
                        corner_state = corner_candidate;
                        corner_pass_count++;
                        corner_candidate = TASK_CHASSIS_CORNER_NONE;
                        corner_enter_count = 0U;
                        corner_hold_count = g_task_chassis_pid.corner.turn_hold_ticks;
                        corner_exit_count = 0U;
                    }
                }
                else if (all_sensors_off && (corner_candidate != TASK_CHASSIS_CORNER_NONE))
                {
                    if (corner_enter_count < 255U)
                    {
                        corner_enter_count++;
                    }

                    if (corner_enter_count >= g_task_chassis_pid.corner.enter_ticks)
                    {
                        corner_state = corner_candidate;
                        corner_pass_count++;
                        corner_candidate = TASK_CHASSIS_CORNER_NONE;
                        corner_enter_count = 0U;
                        corner_hold_count = g_task_chassis_pid.corner.turn_hold_ticks;
                        corner_exit_count = 0U;
                    }
                }
                else
                {
                    corner_candidate = TASK_CHASSIS_CORNER_NONE;
                    corner_enter_count = 0U;
                }
            }
            else
            {
                corner_candidate = TASK_CHASSIS_CORNER_NONE;
                corner_enter_count = 0U;

                if (corner_hold_count > 0U)
                {
                    corner_hold_count--;
                    corner_exit_count = 0U;
                }
                else if ((detected_corner == corner_state) || all_sensors_off)
                {
                    corner_exit_count = 0U;
                }
                else
                {
                    if (corner_exit_count < 255U)
                    {
                        corner_exit_count++;
                    }

                    if (corner_exit_count >= g_task_chassis_pid.corner.exit_ticks)
                    {
                        corner_state = TASK_CHASSIS_CORNER_NONE;
                        corner_exit_count = 0U;
                    }
                }
            }

            if (corner_state == TASK_CHASSIS_CORNER_LEFT)
            {
                cmd_local.left_target_speed = 0;
                cmd_local.right_target_speed = g_task_chassis_pid.corner.turn_speed;
                line_turn_output = (float)g_task_chassis_pid.corner.turn_speed;
            }
            else if (corner_state == TASK_CHASSIS_CORNER_RIGHT)
            {
                cmd_local.left_target_speed = g_task_chassis_pid.corner.turn_speed;
                cmd_local.right_target_speed = 0;
                line_turn_output = -(float)g_task_chassis_pid.corner.turn_speed;
            }
            else
            {
                line_error = cmd_local.line_target - line_weight;
                line_turn_output = line_error * g_task_chassis_pid.line_follow.kp;
                line_turn_output = task_chassis_clamp_float(line_turn_output,
                                                            -g_task_chassis_pid.line_follow.output_max,
                                                            g_task_chassis_pid.line_follow.output_max);

                left_target_speed_raw = cmd_local.base_speed + (int32_t)line_turn_output;
                right_target_speed_raw = cmd_local.base_speed - (int32_t)line_turn_output;
                cmd_local.left_target_speed = task_chassis_apply_target_slew(
                    left_target_speed_raw,
                    last_left_target_speed,
                    g_task_chassis_pid.target_speed_step_max);
                cmd_local.right_target_speed = task_chassis_apply_target_slew(
                    right_target_speed_raw,
                    last_right_target_speed,
                    g_task_chassis_pid.target_speed_step_max);
            }
            }

            PID_Pos_SetTarget(&left_speed_pid, (float)cmd_local.left_target_speed);
            PID_Pos_SetTarget(&right_speed_pid, (float)cmd_local.right_target_speed);

            left_duty_cmd = task_chassis_float_to_duty(
                PID_Pos_Compute(&left_speed_pid, (float)left_actual_speed));
            right_duty_cmd = task_chassis_float_to_duty(
                PID_Pos_Compute(&right_speed_pid, (float)right_actual_speed));

            mod_motor_set_duty(motor_ctx,
                               MOD_MOTOR_LEFT,
                               task_chassis_apply_output_sign(MOD_MOTOR_LEFT, left_duty_cmd));
            mod_motor_set_duty(motor_ctx,
                               MOD_MOTOR_RIGHT,
                               task_chassis_apply_output_sign(MOD_MOTOR_RIGHT, right_duty_cmd));
        }
        else
        {
            PID_Pos_Reset(&left_speed_pid);
            PID_Pos_Reset(&right_speed_pid);
            corner_state = TASK_CHASSIS_CORNER_NONE;
            corner_candidate = TASK_CHASSIS_CORNER_NONE;
            corner_enter_count = 0U;
            corner_hold_count = 0U;
            corner_exit_count = 0U;
            corner_pass_count = 0U;
            last_left_target_speed = 0;
            last_right_target_speed = 0;
            mod_motor_set_duty(motor_ctx, MOD_MOTOR_LEFT, 0);
            mod_motor_set_duty(motor_ctx, MOD_MOTOR_RIGHT, 0);
        }

        last_mode = cmd_local.mode;
        last_left_target_speed = cmd_local.left_target_speed;
        last_right_target_speed = cmd_local.right_target_speed;

        telemetry_local.enabled = (cmd_local.mode != TASK_CHASSIS_CMD_MODE_IDLE);
        telemetry_local.mode = cmd_local.mode;
        telemetry_local.left_encoder_ready = left_encoder_ready;
        telemetry_local.right_encoder_ready = right_encoder_ready;
        telemetry_local.left_target_speed = cmd_local.left_target_speed;
        telemetry_local.right_target_speed = cmd_local.right_target_speed;
        telemetry_local.left_actual_speed = left_actual_speed;
        telemetry_local.right_actual_speed = right_actual_speed;
        telemetry_local.left_total_position = left_total_position;
        telemetry_local.right_total_position = right_total_position;
        telemetry_local.left_duty_cmd = left_duty_cmd;
        telemetry_local.right_duty_cmd = right_duty_cmd;
        telemetry_local.left_encoder_counter_raw = left_encoder_counter_raw;
        telemetry_local.right_encoder_counter_raw = right_encoder_counter_raw;
        telemetry_local.left_encoder_a_level = left_encoder_a_level;
        telemetry_local.left_encoder_b_level = left_encoder_b_level;
        telemetry_local.right_encoder_a_level = right_encoder_a_level;
        telemetry_local.right_encoder_b_level = right_encoder_b_level;
        memcpy(telemetry_local.line_sensor_states, line_sensor_states, sizeof(line_sensor_states));
        telemetry_local.line_weight = line_weight;
        telemetry_local.line_turn_output = line_turn_output;
        task_chassis_publish_telemetry(&telemetry_local);
        task_chassis_publish_telemetry(&telemetry_local);

        osDelay(TASK_CHASSIS_PERIOD_MS);
    }
}
