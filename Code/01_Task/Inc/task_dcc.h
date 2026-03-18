#ifndef FINAL_GRADUATE_WORK_TASK_DCC_H
#define FINAL_GRADUATE_WORK_TASK_DCC_H

#include "cmsis_os2.h"
#include <stdint.h>
#include "mod_motor.h"
#include "mod_sensor.h"
#include "pid_config.h"
#include "pid_pos.h"
#include "pid_inc.h"

/** 底盘控制任务周期（50Hz） */
#define TASK_DCC_PERIOD_MS (20U)

/**
 * @brief 底盘模式枚举定义
 *
 * @details
 * 1. MODE_IDLE：模式0，不做控制输出，仅保持停机。
 * 2. MODE_STRAIGHT：模式1，执行当前编码器直线控制逻辑，并附加灰度触发急停判断。
 * 3. MODE_TRACK_TODO：模式2，黑线循迹预留，当前只保留 TODO，不执行控制。
 */
#define TASK_DCC_MODE_IDLE       (0U) // 模式0：空闲
#define TASK_DCC_MODE_STRAIGHT   (1U) // 模式1：直线控制
#define TASK_DCC_MODE_TRACK_TODO (2U) // 模式2：循迹预留
#define TASK_DCC_MODE_TOTAL      (3U) // 模式总数：用于单击循环切换

/**
 * @brief 模式切换信号量句柄
 * @details
 * 由 KEY3 单击释放，DCC 任务读取后执行 mode 循环切换。
 */
extern osSemaphoreId_t Sem_TaskChangeHandle;

/**
 * @brief ready 取反信号量句柄
 * @details
 * 由 KEY3 双击释放，DCC 任务读取后执行 ready 标志位 0/1 取反。
 */
extern osSemaphoreId_t Sem_ReadyToggleHandle;

/**
 * @brief 底盘控制任务入口函数
 * @param argument 任务参数指针（当前实现中未使用）
 */
void StartDccTask(void *argument);

/**
 * @brief 读取当前 DCC 模式值
 * @return uint8_t 当前模式（0/1/2）
 */
uint8_t task_dcc_get_mode(void);

/**
 * @brief 读取当前 DCC ready 标志位
 * @return uint8_t 当前 ready（0=未就绪，1=就绪）
 */
uint8_t task_dcc_get_ready(void);

#endif
