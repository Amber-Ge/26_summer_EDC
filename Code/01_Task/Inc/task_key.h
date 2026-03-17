/**
 ******************************************************************************
 * @file    task_key.h
 * @brief   按键任务接口定义
 * @details
 * 1. 任务层只调用 mod_key，不直接调用 drv_key。
 * 2. 任务层负责把按键事件转换成业务动作（如释放信号量）。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_KEY_H
#define FINAL_GRADUATE_WORK_TASK_KEY_H

#include "cmsis_os.h"
#include "mod_key.h"

/**
 * @brief 按键任务扫描周期（与模块层保持一致）
 */
#define TASK_KEY_PERIOD_MS (MOD_KEY_SCAN_PERIOD_MS)

/**
 * @brief 按键任务入口函数
 * @param argument 任务参数（当前未使用）
 */
void StartKeyTask(void *argument);

/** GPIO 任务触发信号量（KEY1 单击使用） */
extern osSemaphoreId_t Sem_LEDHandle;

#endif /* FINAL_GRADUATE_WORK_TASK_KEY_H */
