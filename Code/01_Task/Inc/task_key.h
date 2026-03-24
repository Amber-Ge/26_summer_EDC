/**
 * @file    task_key.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   按键任务接口声明。
 * @details
 * 1. KeyTask 是系统输入事件源，负责扫描按键并分发控制信号。
 * 2. 任务事件读取来自 mod_key，业务动作通过信号量解耦到 DccTask/GpioTask。
 * 3. Task 层不直接解析 GPIO 电平，所有去抖和手势识别由模块层完成。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_KEY_H
#define FINAL_GRADUATE_WORK_TASK_KEY_H

#include "cmsis_os.h"
#include "mod_key.h"

/* 按键扫描周期（毫秒），复用模块层统一扫描周期配置 */
#define TASK_KEY_PERIOD_MS (MOD_KEY_SCAN_PERIOD_MS)
/* Key 任务启动开关：1=正常运行，0=启动后挂起 */
#define TASK_KEY_STARTUP_ENABLE (1U)

/* 任意按键事件反馈信号量：由 KeyTask 释放，GpioTask 消费 */
extern osSemaphoreId_t Sem_RedLEDHandle;

/**
 * @brief KeyTask 入口函数。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartKeyTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_KEY_H */

