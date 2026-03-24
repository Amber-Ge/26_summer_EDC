/**
 * @file    task_dcc.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   DCC 底盘任务接口声明。
 * @details
 * 1. 该头文件定义 DCC 状态机常量、任务入口和只读状态查询接口。
 * 2. KeyTask 通过信号量驱动状态切换，DccTask 作为唯一状态写入者。
 * 3. 其他任务仅允许调用查询接口读取状态，不允许直接改写状态机。
 * 4. DccTask 内部会调用 mod_motor/mod_sensor 与 PID 组件完成闭环控制。
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

/* ========================= DCC 任务时序参数 ========================= */

/* 底盘任务主循环周期（毫秒） */
#define TASK_DCC_PERIOD_MS (20U)
/* PREPARE 状态持续时间（毫秒） */
#define TASK_DCC_PREPARE_MS (3000U)
/* DCC 任务启动开关：1=正常运行，0=启动后挂起 */
#define TASK_DCC_STARTUP_ENABLE (1U)
/* DCC VOFA 上报开关：1=发送，0=不发送 */
#define TASK_DCC_VOFA_ENABLE (0U)
/* DCC VOFA 曲线标签（仅在 TASK_DCC_VOFA_ENABLE=1 时生效） */
#define TASK_DCC_VOFA_TAG ("DccState")

/* ========================= DCC 模式定义 ========================= */

/* 空闲模式：只允许停机，不执行底盘控制 */
#define TASK_DCC_MODE_IDLE (0U)
/* 直线模式：位置环 + 速度环闭环控制 */
#define TASK_DCC_MODE_STRAIGHT (1U)
/* 循迹模式：按传感器权重分配左右轮目标速度 */
#define TASK_DCC_MODE_TRACK (2U)
/* 模式总数：用于 KEY3 单击循环切换 */
#define TASK_DCC_MODE_TOTAL (3U)

/* ========================= DCC 运行状态定义 ========================= */

/* 关闭态：电机停机，可切换模式 */
#define TASK_DCC_RUN_OFF (0U)
/* 预备态：等待倒计时结束后自动进入 ON */
#define TASK_DCC_RUN_PREPARE (1U)
/* 运行态：按当前 mode 执行底盘控制 */
#define TASK_DCC_RUN_ON (2U)
/* 保护停机态：由保护策略触发，需人工重新切换 */
#define TASK_DCC_RUN_STOP (3U)

/* KEY3 单击：请求切换 DCC 模式并强制回 OFF */
extern osSemaphoreId_t Sem_TaskChangeHandle;
/* KEY3 双击：请求切换 DCC 运行态（OFF <-> PREPARE/ON） */
extern osSemaphoreId_t Sem_ReadyToggleHandle;
/* KEY2 单击：请求执行 DCC 全重置（mode=IDLE + run_state=OFF） */
extern osSemaphoreId_t Sem_DccHandle;

/**
 * @brief DCC 任务入口函数。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartDccTask(void *argument);

/**
 * @brief 获取当前 DCC 模式。
 * @return `TASK_DCC_MODE_*` 枚举值。
 */
uint8_t task_dcc_get_mode(void);

/**
 * @brief 获取当前 DCC 运行状态。
 * @return `TASK_DCC_RUN_*` 枚举值。
 */
uint8_t task_dcc_get_run_state(void);

/**
 * @brief 兼容旧接口：返回是否处于 ON 运行态。
 * @return ON 返回 1，其他状态返回 0。
 */
uint8_t task_dcc_get_ready(void);

#endif /* FINAL_GRADUATE_WORK_TASK_DCC_H */

