#ifndef FINAL_GRADUATE_WORK_TASK_DCC_H
#define FINAL_GRADUATE_WORK_TASK_DCC_H

#include "cmsis_os2.h"
#include "mod_motor.h"
#include "mod_sensor.h"
#include "pid_config.h"
#include "pid_pos.h"
#include "pid_inc.h"

/** 底盘控制任务周期（50Hz） */
#define TASK_DCC_PERIOD_MS           (20U)

/** 底盘控制模式定义 */
#define TASK_DCC_MODE_BALANCE        (0U) // 模式0：外环位置 + 内环速度
#define TASK_DCC_MODE_TRACK_RESERVED (1U) // 模式1：循迹预留（当前留空）

/**
 * @brief 模式切换信号量句柄
 * @details 由 KEY3 释放，DCC 任务读取后切换控制模式。
 */
extern osSemaphoreId_t Sem_TaskChangeHandle;

/**
 * @brief 底盘控制任务入口函数
 * @param argument 任务参数指针（当前实现中未使用）
 */
void StartDccTask(void *argument);

#endif
