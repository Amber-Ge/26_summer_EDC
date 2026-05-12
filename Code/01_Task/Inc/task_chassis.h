/**
 * @file    task_chassis.h
 * @brief   Chassis task public interface.
 */
#ifndef ZGT6_FREERTOS_TASK_CHASSIS_H
#define ZGT6_FREERTOS_TASK_CHASSIS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    float kp;
    float output_max;
} task_chassis_line_follow_param_t;

typedef struct
{
    int32_t turn_speed;       // single-wheel turn speed while corner mode is active
    uint8_t enter_ticks;      // consecutive ticks required to enter corner mode
    uint8_t turn_hold_ticks;  // minimum locked-turn duration after corner entry
    uint8_t exit_ticks;       // consecutive ticks required to exit corner mode
} task_chassis_corner_param_t;

typedef struct
{
    float kp;
    float ki;
    float kd;
    float output_max;
    float integral_max;
} task_chassis_pid_param_t;

typedef struct
{
    int8_t left_output;
    int8_t right_output;
    int8_t left_feedback;
    int8_t right_feedback;
    int8_t left_display;
    int8_t right_display;
} task_chassis_sign_param_t;

typedef struct
{
    task_chassis_line_follow_param_t line_follow;
    task_chassis_corner_param_t corner;
    task_chassis_pid_param_t left_speed_inner;
    task_chassis_pid_param_t right_speed_inner;
    task_chassis_sign_param_t sign;
    int32_t debug_target_left;
    int32_t debug_target_right;
    int16_t pwm_debug_left_duty;
    int16_t pwm_debug_right_duty;
    int32_t line_follow_base_speed;
    int32_t target_speed_step_max;
    float line_target;
} task_chassis_pid_group_t;

extern const task_chassis_pid_group_t g_task_chassis_pid;

typedef enum
{
    TASK_CHASSIS_CMD_MODE_IDLE = 0,
    TASK_CHASSIS_CMD_MODE_SPEED_DEBUG,
    TASK_CHASSIS_CMD_MODE_PWM_DEBUG,
    TASK_CHASSIS_CMD_MODE_LINE_TRACK,
} task_chassis_cmd_mode_t;

typedef struct
{
    task_chassis_cmd_mode_t mode;
    int32_t left_target_speed;
    int32_t right_target_speed;
    int32_t base_speed;
    float line_target;
    uint8_t lap_target;
} task_chassis_cmd_t;

typedef struct
{
    bool enabled;
    task_chassis_cmd_mode_t mode;
    bool left_encoder_ready;
    bool right_encoder_ready;
    int32_t left_target_speed;
    int32_t right_target_speed;
    int32_t left_actual_speed;
    int32_t right_actual_speed;
    int64_t left_total_position;
    int64_t right_total_position;
    int16_t left_duty_cmd;
    int16_t right_duty_cmd;
    int32_t left_encoder_counter_raw;
    int32_t right_encoder_counter_raw;
    uint8_t left_encoder_a_level;
    uint8_t left_encoder_b_level;
    uint8_t right_encoder_a_level;
    uint8_t right_encoder_b_level;
    uint8_t line_sensor_states[8];
    float line_weight;
    float line_turn_output;
} task_chassis_telemetry_t;

/**
 * @brief 底盘任务主循环。
 * @param argument RTOS 任务入口参数，当前未使用。
 */
void StartChassisTask(void *argument);

/**
 * @brief 设置底盘控制命令。
 * @param cmd 指向待写入命令的指针。
 * @return 写入成功返回 `true`，参数无效返回 `false`。
 */
bool task_chassis_set_command(const task_chassis_cmd_t *cmd);

/**
 * @brief 仅切换底盘工作模式，并按模式补齐默认参数。
 * @param mode 目标模式。
 * @return 设置成功返回 `true`，模式非法返回 `false`。
 */
bool task_chassis_set_mode(task_chassis_cmd_mode_t mode);

/**
 * @brief 设置速度调试模式下的左右轮目标速度。
 * @param left_target_speed 左轮目标速度。
 * @param right_target_speed 右轮目标速度。
 * @return 设置成功返回 `true`。
 */
bool task_chassis_set_speed_debug_target(int32_t left_target_speed, int32_t right_target_speed);

/**
 * @brief 设置 PWM 调试模式下的左右轮占空比。
 * @param left_duty 左轮占空比命令。
 * @param right_duty 右轮占空比命令。
 * @return 设置成功返回 `true`。
 */
bool task_chassis_set_pwm_debug_duty(int16_t left_duty, int16_t right_duty);

/**
 * @brief 设置循线模式的基础速度和目标线位置。
 * @param base_speed 循线基础速度。
 * @param line_target 目标线权重位置。
 * @return 设置成功返回 `true`。
 */
bool task_chassis_set_line_track_target(int32_t base_speed, float line_target);

/**
 * @brief 设置循线模式参数，并指定圈数目标。
 * @param base_speed 循线基础速度。
 * @param line_target 目标线权重位置。
 * @param lap_target 目标圈数，`0` 表示不限圈数持续运行。
 * @return 设置成功返回 `true`。
 */
bool task_chassis_set_line_track_laps(int32_t base_speed, float line_target, uint8_t lap_target);

/**
 * @brief 读取底盘遥测信息快照。
 * @param telemetry 用于接收遥测数据的输出指针。
 * @return 读取成功返回 `true`，参数无效返回 `false`。
 */
bool task_chassis_get_telemetry(task_chassis_telemetry_t *telemetry);

#endif /* ZGT6_FREERTOS_TASK_CHASSIS_H */
