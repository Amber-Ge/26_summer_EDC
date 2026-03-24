/**
 * @file    task_key.h
 * @brief   按键任务接口声明。
 * @details
 * 1. 文件作用：定义按键任务入口与扫描节拍参数，作为输入事件源向其他任务分发控制信号。
 * 2. 上层绑定：由 RTOS 创建并周期调度；输出信号量由 DCC/GPIO 任务消费。
 * 3. 下层依赖：`mod_key` 提供去抖后的按键事件枚举。
 * 4. 生命周期：任务常驻，持续执行“扫描 -> 事件映射 -> 信号量释放”流程。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_KEY_H
#define FINAL_GRADUATE_WORK_TASK_KEY_H

#include "cmsis_os.h"
#include "mod_key.h"

#define TASK_KEY_PERIOD_MS (MOD_KEY_SCAN_PERIOD_MS)

void StartKeyTask(void *argument);

/* 任意按键事件反馈信号量（由 KeyTask 释放，GpioTask 用于黄灯短闪） */
extern osSemaphoreId_t Sem_RedLEDHandle; // RTOS 信号量句柄，用于任务同步。

#endif /* FINAL_GRADUATE_WORK_TASK_KEY_H */

