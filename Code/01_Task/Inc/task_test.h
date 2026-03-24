/**
 * @file    task_test.h
 * @brief   测试任务接口声明。
 * @details
 * 1. 文件作用：定义测试任务节拍参数并声明调试任务入口。
 * 2. 上层绑定：由 RTOS 创建并调度，主要用于联调阶段临时验证。
 * 3. 下层依赖：可按需调用 VOFA/传感器/电机等模块接口，不承载正式业务链路。
 * 4. 生命周期：默认常驻但低活跃度，保留为可开关的调试扩展点。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_CONTROL_H
#define FINAL_GRADUATE_WORK_TASK_CONTROL_H

#include "cmsis_os.h"
#include "mod_motor.h"
#include "mod_sensor.h"
#include "mod_stepper.h"
#include "mod_vofa.h"
#include "pid_config.h"
#include "pid_inc.h"

#define TASK_TEST_PERIOD_MS    (1000000U)  // 测试任务循环周期（ms）
#define TASK_TEST_PREPARE_MS   (5000U)     // 预留：测试启动前准备时间（ms）
#define TASK_TEST_TARGET_SPEED (40)     // 预留：测试默认目标速度

void StartTestTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_CONTROL_H */

