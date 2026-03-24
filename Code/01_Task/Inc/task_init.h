/**
 * @file    task_init.h
 * @brief   系统初始化任务接口声明。
 * @details
 * 1. 文件作用：声明系统初始化任务与初始化流程入口。
 * 2. 上下层绑定：上层由 RTOS 启动流程触发；下层依赖各 Module/Driver 初始化接口。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_INIT_H
#define FINAL_GRADUATE_WORK_TASK_INIT_H

#include "cmsis_os.h"

/**
 * @brief InitTask 入口函数。
 *
 * @details
 * 该任务只负责“系统一次性初始化”：
 * 1. 显式绑定所有非 UART / 非 OLED 的硬件映射（LED、继电器、电机、循迹传感器）。
 * 2. 调用对应模块初始化接口，建立稳定的上电状态。
 * 3. 初始化完成后释放 Sem_Init，作为全系统启动门控信号。
 * 4. 任务完成使命后自删除，不再参与调度，避免占用 CPU 与栈资源。
 *
 * @param argument 任务参数（当前实现未使用）。
 */
void StartInitTask(void *argument);

/**
 * @brief 统一的“初始化完成等待”门控函数。
 *
 * @details
 * 其他业务任务（GPIO、KEY、DCC、PC、STEPPER、TEST、DEFAULT）在进入各自主循环前
 * 都应先调用该函数。函数行为如下：
 * 1. 阻塞等待 Sem_Init（由 InitTask 在初始化完成后释放）。
 * 2. 一旦成功获取信号量，立即再释放一次，形成“闸门常开”效果。
 * 3. 后续任何任务调用此函数都可快速通过，不会再次被长期阻塞。
 *
 * @note
 * 该设计本质是“单次初始化 -> 多任务共享通过权”，避免每个任务重复初始化硬件。
 */
void task_wait_init_done(void);

#endif /* FINAL_GRADUATE_WORK_TASK_INIT_H */

