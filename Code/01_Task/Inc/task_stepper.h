/**
 * @file    task_stepper.h
 * @brief   步进电机任务接口声明。
 * @details
 * 1. 文件作用：定义步进任务调度参数、控制参数、状态快照结构和对外控制接口。
 * 2. 上层绑定：`StartStepperTask` 由 RTOS 调度；运行时读取 `task_dcc_get_*` 状态并消费 K230 最新帧。
 * 3. 下层依赖：`mod_stepper`（指令编解码与串口发送）、`mod_k230`（视觉误差输入）、
 *    `mod_vofa`（状态上报）以及 UART 资源。
 * 4. 资源边界：串口与互斥锁在 `InitTask`/`task_stepper_prepare_channels` 阶段注入，任务主循环只消费绑定结果。
 * 5. 生命周期：任务常驻运行，按周期执行“收帧 -> 闭环 -> 保护 -> 上报”流程。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_STEPPER_H
#define FINAL_GRADUATE_WORK_TASK_STEPPER_H

#include "cmsis_os.h"
#include "mod_k230.h"
#include "mod_stepper.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 任务调度参数 ========================= */

/* Stepper 任务执行周期（毫秒） */
#define TASK_STEPPER_PERIOD_MS (10U)

/* 连续多少个周期没有新视觉帧后触发停机保护 */
#define TASK_STEPPER_NO_FRAME_STOP_CYCLES (3U)

/* 任务启动开关：1=启动后正常运行，0=启动后挂起 */
#define TASK_STEPPER_STARTUP_ENABLE (1U)

/* 发送到 VOFA 的曲线标签（当前发送 err1/err2） */
#define TASK_STEPPER_VOFA_TAG ("StepperErr")

/* ========================= 轴 ID 定义 ========================= */

/* 与 K230 协议约定：id=1 为 X 轴，id=2 为 Y 轴 */
#define TASK_STEPPER_AXIS_X_ID (1U)
#define TASK_STEPPER_AXIS_Y_ID (2U)

/* 轴使能开关：可用于联调时单独屏蔽某一轴下发 */
#define TASK_STEPPER_ENABLE_X_AXIS (1U)
#define TASK_STEPPER_ENABLE_Y_AXIS (1U)

/* ========================= 串口与驱动地址 ========================= */

/* 步进驱动地址（当前两轴都为 1） */
#define TASK_STEPPER_X_DRIVER_ADDR (1U)
#define TASK_STEPPER_Y_DRIVER_ADDR (1U)

/* ========================= 控制参数（可调） ========================= */

/* 细分与每圈脉冲数：200 * 16 = 3200 pulse/rev */
#define TASK_STEPPER_PULSE_PER_REV (3200U)

/* 速度模式最大速度（rpm） */
#define TASK_STEPPER_VEL_MAX_RPM (50U)

/* 位置模式最大速度（rpm） */
#define TASK_STEPPER_POS_MAX_RPM (300U)

/* 位置 PID 参数（输出单位：每周期脉冲数） */
#define TASK_STEPPER_POS_PID_KP (0.20f)
#define TASK_STEPPER_POS_PID_KI (0.0012f)
#define TASK_STEPPER_POS_PID_KD (0.00f)

/* PID 输出限幅（每周期最大脉冲命令） */
#define TASK_STEPPER_POS_PID_OUT_MAX_PULSE (200.0f)

/* PID 积分限幅 */
#define TASK_STEPPER_POS_PID_INTEGRAL_MAX (6000.0f)

/* 误差死区：|err| < deadband 时停机 */
#define TASK_STEPPER_ERR_DEADBAND (1)

/* 小误差阈值：|err| <= 阈值时仅使用 Kp（关闭积分） */
#define TASK_STEPPER_KP_ONLY_ERR_THRESH (10)

/* 小误差区 Kp 增益（单位：pulse/cycle per err） */
#define TASK_STEPPER_KP_ONLY_KP (0.12f)

/* mode1（STRAIGHT）前馈开关与参数
 * 前馈来源：底盘编码器相邻采样差分
 * 前馈输出：与 PID 输出相加后作为最终脉冲命令
 */
#define TASK_STEPPER_MODE1_FEEDFORWARD_ENABLE (1U)
#define TASK_STEPPER_MODE1_FEEDFORWARD_X_K (0.00f)
#define TASK_STEPPER_MODE1_FEEDFORWARD_Y_K (0.10f)
#define TASK_STEPPER_MODE1_FEEDFORWARD_MAX_PULSE (80.0f)

/* Y 轴固定位置测试模式（调试开关） */
#define TASK_STEPPER_Y_FIXED_POSITION_ENABLE (0U)
#define TASK_STEPPER_Y_FIXED_SPEED_RPM (50U)
#define TASK_STEPPER_Y_FIXED_PULSE (100U)
#define TASK_STEPPER_Y_FIXED_DIR_CW (1U)

/* X 轴软限位范围：[-800, +800] */
#define TASK_STEPPER_X_LIMIT_PULSE (800)

/* Y 轴软限位范围：[-400, +400] */
#define TASK_STEPPER_Y_LIMIT_PULSE (400)

/* 预留参数：动作完成额外保护时间（当前未使用） */
#define TASK_STEPPER_MOVE_DONE_MARGIN_MS (5U)

/* 方向反相开关：0=不反相，1=反相 */
#define TASK_STEPPER_X_DIR_INVERT (0U)
#define TASK_STEPPER_Y_DIR_INVERT (0U)

/* ========================= 状态结构体 ========================= */

/**
 * @brief Stepper 任务只读状态快照
 */
typedef struct
{
    bool configured;             // 任务资源初始化是否完成
    bool k230_bound;             // K230 通道是否已绑定
    bool vofa_bound;             // VOFA 通道是否已绑定
    bool x_axis_bound;           // X 轴驱动通道是否已绑定
    bool y_axis_bound;           // Y 轴驱动通道是否已绑定

    uint8_t dcc_mode;            // 当前 DCC 模式（IDLE/STRAIGHT/TRACK）
    uint8_t dcc_run_state;       // 当前 DCC 运行状态（OFF/PREPARE/ON/STOP）

    uint8_t motor1_id;           // 最新帧 motor1_id
    int16_t err1;                // 最新帧 err1（视觉误差）
    uint8_t motor2_id;           // 最新帧 motor2_id
    int16_t err2;                // 最新帧 err2（视觉误差）

    int32_t x_pos_pulse;         // X 轴任务层估计位置（pulse）
    int32_t y_pos_pulse;         // Y 轴任务层估计位置（pulse）
    uint8_t x_busy;              // 预留字段
    uint8_t y_busy;              // 预留字段

    uint32_t x_last_pulse_cmd;   // X 轴最近一次等效脉冲命令
    uint32_t y_last_pulse_cmd;   // Y 轴最近一次等效脉冲命令
    uint16_t x_last_speed_cmd;   // X 轴最近一次速度命令（rpm）
    uint16_t y_last_speed_cmd;   // Y 轴最近一次速度命令（rpm）

    uint32_t frame_update_count; // 接收到新视觉帧次数
    uint32_t x_cmd_ok_count;     // X 轴命令成功次数
    uint32_t y_cmd_ok_count;     // Y 轴命令成功次数
    uint32_t x_cmd_drop_count;   // X 轴命令失败/丢弃次数
    uint32_t y_cmd_drop_count;   // Y 轴命令失败/丢弃次数
    uint32_t vofa_tx_ok_count;   // VOFA 发送成功次数
    uint32_t vofa_tx_drop_count; // VOFA 发送失败次数
} task_stepper_state_t;

/* ========================= 对外接口 ========================= */

bool task_stepper_prepare_channels(void);
bool task_stepper_is_ready(void);
void StartStepperTask(void *argument);
const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id);

/**
 * @brief 对外速度命令接口（内部会限幅到 TASK_STEPPER_VEL_MAX_RPM）
 */
bool task_stepper_send_velocity(uint8_t logic_id,
                                mod_stepper_dir_e dir,
                                uint16_t vel_rpm,
                                uint8_t acc,
                                bool sync_flag);

/**
 * @brief 对外位置命令接口（内部会限幅到 TASK_STEPPER_POS_MAX_RPM）
 */
bool task_stepper_send_position(uint8_t logic_id,
                                mod_stepper_dir_e dir,
                                uint16_t vel_rpm,
                                uint8_t acc,
                                uint32_t pulse,
                                bool absolute_mode,
                                bool sync_flag);

#endif /* FINAL_GRADUATE_WORK_TASK_STEPPER_H */

