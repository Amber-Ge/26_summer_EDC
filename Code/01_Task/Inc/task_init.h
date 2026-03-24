/**
 * @file    task_init.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   系统初始化任务接口声明。
 * @details
 * 1. InitTask 负责硬件映射绑定、模块初始化和启动闸门释放。
 * 2. 其他业务任务通过 task_wait_init_done 与初始化流程解耦。
 * 3. InitTask 执行完成后自删除，不参与后续业务调度。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_INIT_H
#define FINAL_GRADUATE_WORK_TASK_INIT_H

#include "cmsis_os.h"

/**
 * @brief InitTask 入口函数。
 * @details
 * 1. 绑定 LED/Relay/Key/Sensor/Motor/Battery 等模块默认上下文。
 * 2. 执行模块初始化并写入上电安全输出状态。
 * 3. 绑定 K230/VOFA/Stepper 运行通道。
 * 4. 释放初始化闸门信号量后自删除线程。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartInitTask(void *argument);

/**
 * @brief 等待初始化完成闸门。
 * @details
 * 1. 当 Sem_InitHandle 尚未释放时阻塞等待。
 * 2. 首次通过后立即回填一次信号量，实现后续任务快速通过。
 * 3. 该函数应在各业务任务主循环前调用一次。
 */
void task_wait_init_done(void);

#endif /* FINAL_GRADUATE_WORK_TASK_INIT_H */

