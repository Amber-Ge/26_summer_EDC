/**
 ******************************************************************************
 * @file    task_stepper.h
 * @brief   Stepper任务层接口（与DCC运行态同步、20ms闭环位置控制）
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_STEPPER_H
#define FINAL_GRADUATE_WORK_TASK_STEPPER_H

#include "cmsis_os.h"
#include "mod_k230.h"
#include "mod_stepper.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 任务调度参数 ========================= */

/* Stepper任务循环周期（ms） */
#define TASK_STEPPER_PERIOD_MS (20U)

/* Stepper任务启动开关：1=运行，0=启动后挂起 */
#define TASK_STEPPER_STARTUP_ENABLE (1U)

/* VOFA输出标签 */
#define TASK_STEPPER_VOFA_TAG ("StepperCtl")

/* ========================= 轴与ID定义 ========================= */

/* K230与任务层约定：id=1 为X轴，id=2 为Y轴 */
#define TASK_STEPPER_AXIS_X_ID (1U)
#define TASK_STEPPER_AXIS_Y_ID (2U)

/* ========================= 串口与驱动地址参数 ========================= */

/* X/Y轴驱动地址（按你的要求，两轴都用地址1） */
#define TASK_STEPPER_X_DRIVER_ADDR (1U)
#define TASK_STEPPER_Y_DRIVER_ADDR (1U)

/* ========================= 控制参数（可调） ========================= */

/* 细分后每圈脉冲数：16细分 -> 3200脉冲/圈 */
#define TASK_STEPPER_PULSE_PER_REV (3200U)

/* 任务层速度上限（rpm）：超过时强制裁剪到该值 */
#define TASK_STEPPER_VEL_MAX_RPM (100U)

/* mode=1 的误差转脉冲系数：pulse = abs(err) * K */
#define TASK_STEPPER_ERR2VEL_K_MODE1 (1.0f)

/* mode=2 的误差转脉冲系数：pulse = abs(err) * K */
#define TASK_STEPPER_ERR2VEL_K_MODE2 (1.0f)

#define TASK_STEPPER_ERR_DEADBAND (2)

/* X轴软件限位（累计脉冲绝对值）：范围[-800, +800]，全行程1600脉冲 */
#define TASK_STEPPER_X_LIMIT_PULSE (800)

/* Y轴软件限位（累计脉冲绝对值）：范围[-400, +400]，全行程800脉冲 */
#define TASK_STEPPER_Y_LIMIT_PULSE (400)

/* 运动完成保护的额外裕量时间（ms） */
#define TASK_STEPPER_MOVE_DONE_MARGIN_MS (5U)

/* 方向映射反相开关：0=不反相，1=反相
 * 你当前要求：
 * - 负误差时 X 轴应走 CW -> 需要反相（设为1）
 * - 负误差时 Y 轴应走 CCW -> 保持默认不反相（设为0）
 */
#define TASK_STEPPER_X_DIR_INVERT (1U)
#define TASK_STEPPER_Y_DIR_INVERT (0U)

/* ========================= 状态结构体 ========================= */

/**
 * @brief Stepper任务观测状态（只读快照）
 */
typedef struct
{
    bool configured;               /* 任务资源是否已准备完成 */
    bool k230_bound;               /* K230是否已绑定 */
    bool vofa_bound;               /* VOFA是否已绑定 */
    bool x_axis_bound;             /* X轴stepper通道是否已绑定 */
    bool y_axis_bound;             /* Y轴stepper通道是否已绑定 */

    uint8_t dcc_mode;              /* 当前镜像的DCC模式（0/1/2） */
    uint8_t dcc_run_state;         /* 当前镜像的DCC运行态（OFF/PREPARE/ON/STOP） */

    uint8_t motor1_id;             /* 最近一帧K230的motor1_id */
    int16_t err1;                  /* 最近一帧K230的err1 */
    uint8_t motor2_id;             /* 最近一帧K230的motor2_id */
    int16_t err2;                  /* 最近一帧K230的err2 */

    int32_t x_pos_pulse;           /* X轴累计脉冲位置（软件估计） */
    int32_t y_pos_pulse;           /* Y轴累计脉冲位置（软件估计） */
    uint8_t x_busy;                /* X轴是否处于“运动完成保护”窗口 */
    uint8_t y_busy;                /* Y轴是否处于“运动完成保护”窗口 */

    uint32_t x_last_pulse_cmd;     /* X轴最近一次下发脉冲 */
    uint32_t y_last_pulse_cmd;     /* Y轴最近一次下发脉冲 */
    uint16_t x_last_speed_cmd;     /* X轴最近一次下发速度（rpm） */
    uint16_t y_last_speed_cmd;     /* Y轴最近一次下发速度（rpm） */

    uint32_t frame_update_count;   /* 成功获取K230新帧次数 */
    uint32_t x_cmd_ok_count;       /* X轴位置命令下发成功次数 */
    uint32_t y_cmd_ok_count;       /* Y轴位置命令下发成功次数 */
    uint32_t x_cmd_drop_count;     /* X轴命令丢弃次数（忙/限位/异常） */
    uint32_t y_cmd_drop_count;     /* Y轴命令丢弃次数（忙/限位/异常） */
    uint32_t vofa_tx_ok_count;     /* VOFA发送成功次数 */
    uint32_t vofa_tx_drop_count;   /* VOFA发送失败次数 */
} task_stepper_state_t;

/* ========================= 任务接口 ========================= */

bool task_stepper_prepare_channels(void);
bool task_stepper_is_ready(void);
void StartStepperTask(void *argument);
const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id);

/**
 * @brief 预留接口：发送速度命令（任务层限速到200rpm）
 */
bool task_stepper_send_velocity(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag);

/**
 * @brief 预留接口：发送位置命令（任务层限速到200rpm）
 */
bool task_stepper_send_position(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag);

#endif /* FINAL_GRADUATE_WORK_TASK_STEPPER_H */
