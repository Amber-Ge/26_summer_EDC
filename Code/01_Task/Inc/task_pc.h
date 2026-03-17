/**
 ******************************************************************************
 * @file    task_pc.h
 * @brief   PC 通信任务接口定义
 * @details
 * 该任务用于处理 MCU 与上位机（如 VOFA）的串口通信调度。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_PC_H
#define FINAL_GRADUATE_WORK_TASK_PC_H // 头文件防重复包含宏

#include "cmsis_os.h"
#include "drv_uart.h"
#include <stdint.h>

/** PC 任务周期，单位 ms */
#define TASK_PC_PERIOD_MS    (20U) // PC 任务运行周期，单位 ms
/** PC 通信默认串口句柄 */
#define TASK_PC_HUART        (&huart3) // PC 通信默认串口句柄

/**
 * @brief PC 通信任务入口函数。
 * @details
 * 由 RTOS 创建后常驻运行，用于处理命令接收、数据回传和状态同步。
 *
 * @param argument 任务参数指针，当前实现中通常未使用。
 */
void StartPcTask(void *argument);

/**
 * @brief PC 任务事件信号量句柄。
 */
extern osSemaphoreId_t Sem_PcHandle;

/**
 * @brief PC 串口发送互斥锁句柄。
 * @details
 * 多任务共用发送通道时用于串口发送互斥保护。
 */
extern osMutexId_t PcMutexHandle;

#endif /* FINAL_GRADUATE_WORK_TASK_PC_H */
