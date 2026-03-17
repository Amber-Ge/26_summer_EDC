/**
 ******************************************************************************
 * @file    task_key.h
 * @brief   按键任务接口定义
 * @details
 * 该任务以固定周期调用驱动层按键扫描接口，将驱动层输出的按键事件
 * 转换为应用层业务动作（如模式切换、信号量触发等）。
 ******************************************************************************
 */

#ifndef FINAL_GRADUATE_WORK_TASK_KEY_H
#define FINAL_GRADUATE_WORK_TASK_KEY_H

#include "cmsis_os.h"
#include "drv_key.h"

#define TASK_KEY_PERIOD_MS 10 // 按键任务扫描周期，单位 ms

/**
 * @brief 按键任务入口函数。
 * @details
 * 由 RTOS 创建后常驻运行，固定周期调用 `drv_key_scan` 获取按键事件，
 * 并在应用层进行事件分发处理。
 *
 * @param argument 任务参数指针，当前实现中通常未使用。
 */
void StartKeyTask(void *argument);

/**
 * @brief GPIO 任务触发信号量句柄。
 * @details
 * 当其他任务或中断释放该信号量时，GPIO 任务开始执行一次业务动作。
 */
extern osSemaphoreId_t Sem_LEDHandle;


#endif /* FINAL_GRADUATE_WORK_TASK_KEY_H */

