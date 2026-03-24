/**
 * @file    task_dcc.h
 * @brief   DCC 控制任务接口声明。
 * @details
 * 1. 文件作用：定义 DCC 任务状态机常量、任务入口和跨任务只读状态查询接口。
 * 2. 上层绑定：`KeyTask` 通过信号量触发模式/运行态切换；`GpioTask`、`OledTask`、`StepperTask`
 *    通过 `task_dcc_get_*` 读取状态，不直接改写内部状态机。
 * 3. 下层依赖：`mod_motor`/`mod_sensor` 和 PID 组件用于执行底盘控制闭环。
 * 4. 生命周期：该接口在系统启动后全局有效，内部状态由 `StartDccTask` 周期维护。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_DCC_H
#define FINAL_GRADUATE_WORK_TASK_DCC_H

#include "cmsis_os2.h"
#include "mod_motor.h"
#include "mod_sensor.h"
#include "pid_config.h"
#include "pid_inc.h"
#include "pid_pos.h"
#include <stdint.h>

/* ========================= DCC任务时序参数 ========================= */
#define TASK_DCC_PERIOD_MS (20U)   /* 底盘任务主循环周期 */
#define TASK_DCC_PREPARE_MS (3000U) /* 预备态等待时间（双击后等待3秒再进入ON） */

/* ========================= DCC模式定义 ========================= */
#define TASK_DCC_MODE_IDLE (0U)      /* 交互静止模式：电机不允许动作 */
#define TASK_DCC_MODE_STRAIGHT (1U)  /* 直线模式 */
#define TASK_DCC_MODE_TRACK (2U)     /* 循迹模式 */
#define TASK_DCC_MODE_TOTAL (3U)     /* 模式总数（用于按键循环切换） */

/* ========================= DCC运行状态定义 ========================= */
#define TASK_DCC_RUN_OFF (0U)      /* 关闭态：电机停机，可切模式 */
#define TASK_DCC_RUN_PREPARE (1U)  /* 预备态：等待3秒，倒计时结束后进ON */
#define TASK_DCC_RUN_ON (2U)       /* 运行态：按当前mode执行底盘控制 */
#define TASK_DCC_RUN_STOP (3U)     /* 停机告警态：由保护逻辑触发（红闪+蜂鸣） */

/* KEY3 单击：切换 mode 并强制回 OFF（由 KeyTask 释放，DccTask 消费） */
extern osSemaphoreId_t Sem_TaskChangeHandle; // RTOS 信号量句柄，用于任务同步。
/* KEY3 双击：运行状态切换请求（由 KeyTask 释放，DccTask 消费） */
extern osSemaphoreId_t Sem_ReadyToggleHandle; // RTOS 信号量句柄，用于任务同步。
/* KEY2 单击：DCC 全重置请求（mode=0 + run_state=OFF） */
extern osSemaphoreId_t Sem_DccHandle; // RTOS 信号量句柄，用于任务同步。

void StartDccTask(void *argument);

/* 获取当前选择的底盘模式（0/1/2） */
uint8_t task_dcc_get_mode(void);
/* 获取当前运行状态（OFF/PREPARE/ON/STOP） */
uint8_t task_dcc_get_run_state(void);

/* 兼容旧接口：仅当运行状态为ON时返回1，否则返回0 */
uint8_t task_dcc_get_ready(void);

#endif /* FINAL_GRADUATE_WORK_TASK_DCC_H */

