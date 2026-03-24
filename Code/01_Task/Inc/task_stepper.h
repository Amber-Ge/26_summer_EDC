/**
 * @file    task_stepper.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   步进电机任务接口声明。
 * @details
 * 1. 该头文件定义 Stepper 任务调度参数、控制参数、状态快照结构及对外控制接口。
 * 2. StepperTask 读取 DCC 运行状态与 K230 视觉帧，向 mod_stepper 下发双轴命令。
 * 3. 串口句柄和互斥锁在 InitTask/prepare 阶段注入，主循环只消费绑定结果。
 * 4. 状态查询接口只读，业务层禁止直接改写任务内部轴状态。
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
/* 连续无新视觉帧触发保护停机阈值（周期数） */
#define TASK_STEPPER_NO_FRAME_STOP_CYCLES (3U)
/* 任务启动开关：1=正常运行，0=启动后挂起 */
#define TASK_STEPPER_STARTUP_ENABLE (1U)
/* Stepper VOFA 上报开关：1=发送，0=不发送 */
#define TASK_STEPPER_VOFA_ENABLE (1U)
/* VOFA 曲线标签（当前发送 err1/err2） */
#define TASK_STEPPER_VOFA_TAG ("StepperErr")

/* ========================= 轴 ID 与使能配置 ========================= */

/* 与 K230 协议约定：1=X 轴，2=Y 轴 */
#define TASK_STEPPER_AXIS_X_ID (1U)
#define TASK_STEPPER_AXIS_Y_ID (2U)

/* 轴编译期开关：用于单轴联调时屏蔽命令下发 */
#define TASK_STEPPER_ENABLE_X_AXIS (1U)
#define TASK_STEPPER_ENABLE_Y_AXIS (1U)

/* ========================= 驱动地址配置 ========================= */

/* 步进驱动地址（当前两轴均为 1） */
#define TASK_STEPPER_X_DRIVER_ADDR (1U)
#define TASK_STEPPER_Y_DRIVER_ADDR (1U)

/* ========================= 控制参数配置 ========================= */

/* 每圈脉冲数（200 步 * 16 细分） */
#define TASK_STEPPER_PULSE_PER_REV (3200U)

/* 速度模式最大速度（rpm） */
#define TASK_STEPPER_VEL_MAX_RPM (50U)
/* 位置模式最大速度（rpm） */
#define TASK_STEPPER_POS_MAX_RPM (300U)

/* 位置 PID 参数（输出单位：pulse/cycle） */
#define TASK_STEPPER_POS_PID_KP (0.20f)
#define TASK_STEPPER_POS_PID_KI (0.0012f)
#define TASK_STEPPER_POS_PID_KD (0.00f)

/* PID 输出和积分限幅 */
#define TASK_STEPPER_POS_PID_OUT_MAX_PULSE (200.0f)
#define TASK_STEPPER_POS_PID_INTEGRAL_MAX (6000.0f)

/* 误差死区阈值：|err| < deadband 触发停机 */
#define TASK_STEPPER_ERR_DEADBAND (1)
/* 小误差阈值：|err| <= 阈值时仅使用 Kp */
#define TASK_STEPPER_KP_ONLY_ERR_THRESH (10)
/* 小误差区 Kp 增益 */
#define TASK_STEPPER_KP_ONLY_KP (0.12f)

/* STRAIGHT 模式前馈参数：前馈输入为底盘编码器差分 */
#define TASK_STEPPER_MODE1_FEEDFORWARD_ENABLE (1U)
#define TASK_STEPPER_MODE1_FEEDFORWARD_X_K (0.00f)
#define TASK_STEPPER_MODE1_FEEDFORWARD_Y_K (0.10f)
#define TASK_STEPPER_MODE1_FEEDFORWARD_MAX_PULSE (80.0f)

/* Y 轴固定位置调试开关与参数 */
#define TASK_STEPPER_Y_FIXED_POSITION_ENABLE (0U)
#define TASK_STEPPER_Y_FIXED_SPEED_RPM (50U)
#define TASK_STEPPER_Y_FIXED_PULSE (100U)
#define TASK_STEPPER_Y_FIXED_DIR_CW (1U)

/* 软限位范围（pulse） */
#define TASK_STEPPER_X_LIMIT_PULSE (800)
#define TASK_STEPPER_Y_LIMIT_PULSE (400)

/* 预留参数：动作完成额外保护时间（当前未使用） */
#define TASK_STEPPER_MOVE_DONE_MARGIN_MS (5U)

/* 方向反相开关：0=不反相，1=反相 */
#define TASK_STEPPER_X_DIR_INVERT (0U)
#define TASK_STEPPER_Y_DIR_INVERT (0U)

/* ========================= 状态结构体 ========================= */

/**
 * @brief Stepper 任务只读状态快照。
 */
typedef struct
{
    bool configured;             /* 任务资源初始化是否完成 */
    bool k230_bound;             /* K230 通道是否已绑定 */
    bool vofa_bound;             /* VOFA 通道是否已绑定 */
    bool x_axis_bound;           /* X 轴驱动通道是否已绑定 */
    bool y_axis_bound;           /* Y 轴驱动通道是否已绑定 */

    uint8_t dcc_mode;            /* 当前 DCC 模式（IDLE/STRAIGHT/TRACK） */
    uint8_t dcc_run_state;       /* 当前 DCC 运行状态（OFF/PREPARE/ON/STOP） */

    uint8_t motor1_id;           /* 最新帧 motor1_id */
    int16_t err1;                /* 最新帧 err1（视觉误差） */
    uint8_t motor2_id;           /* 最新帧 motor2_id */
    int16_t err2;                /* 最新帧 err2（视觉误差） */

    int32_t x_pos_pulse;         /* X 轴任务层估计位置（pulse） */
    int32_t y_pos_pulse;         /* Y 轴任务层估计位置（pulse） */
    uint8_t x_busy;              /* 预留字段 */
    uint8_t y_busy;              /* 预留字段 */

    uint32_t x_last_pulse_cmd;   /* X 轴最近一次等效脉冲命令 */
    uint32_t y_last_pulse_cmd;   /* Y 轴最近一次等效脉冲命令 */
    uint16_t x_last_speed_cmd;   /* X 轴最近一次速度命令（rpm） */
    uint16_t y_last_speed_cmd;   /* Y 轴最近一次速度命令（rpm） */

    uint32_t frame_update_count; /* 接收到新视觉帧次数 */
    uint32_t x_cmd_ok_count;     /* X 轴命令成功次数 */
    uint32_t y_cmd_ok_count;     /* Y 轴命令成功次数 */
    uint32_t x_cmd_drop_count;   /* X 轴命令失败/丢弃次数 */
    uint32_t y_cmd_drop_count;   /* Y 轴命令失败/丢弃次数 */
    uint32_t vofa_tx_ok_count;   /* VOFA 发送成功次数 */
    uint32_t vofa_tx_drop_count; /* VOFA 发送失败次数 */
} task_stepper_state_t;

/* ========================= 对外接口 ========================= */

/**
 * @brief 准备 Stepper 双轴通道（绑定串口、地址并初始化 PID）。
 * @return 至少完成当前使能轴绑定时返回 true。
 */
bool task_stepper_prepare_channels(void);

/**
 * @brief 查询 Stepper 通道是否已准备完成。
 * @return 已准备完成返回 true，否则返回 false。
 */
bool task_stepper_is_ready(void);

/**
 * @brief Stepper 任务入口。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartStepperTask(void *argument);

/**
 * @brief 获取 Stepper 任务只读状态快照。
 * @param logic_id 预留参数（当前实现忽略）。
 * @return 初始化完成时返回快照地址，否则返回 NULL。
 */
const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id);

/**
 * @brief 对外速度命令接口（内部限幅到 TASK_STEPPER_VEL_MAX_RPM）。
 * @param logic_id 逻辑轴 ID（1=X，2=Y）。
 * @param dir 目标方向。
 * @param vel_rpm 目标速度（rpm）。
 * @param acc 协议加速度参数。
 * @param sync_flag 协议同步标志。
 * @return 发送成功返回 true，否则返回 false。
 */
bool task_stepper_send_velocity(uint8_t logic_id,
                                mod_stepper_dir_e dir,
                                uint16_t vel_rpm,
                                uint8_t acc,
                                bool sync_flag);

/**
 * @brief 对外位置命令接口（内部限幅到 TASK_STEPPER_POS_MAX_RPM）。
 * @param logic_id 逻辑轴 ID（1=X，2=Y）。
 * @param dir 目标方向。
 * @param vel_rpm 目标速度（rpm）。
 * @param acc 协议加速度参数。
 * @param pulse 位置命令脉冲数。
 * @param absolute_mode 是否绝对位置模式。
 * @param sync_flag 协议同步标志。
 * @return 发送成功返回 true，否则返回 false。
 */
bool task_stepper_send_position(uint8_t logic_id,
                                mod_stepper_dir_e dir,
                                uint16_t vel_rpm,
                                uint8_t acc,
                                uint32_t pulse,
                                bool absolute_mode,
                                bool sync_flag);

#endif /* FINAL_GRADUATE_WORK_TASK_STEPPER_H */

