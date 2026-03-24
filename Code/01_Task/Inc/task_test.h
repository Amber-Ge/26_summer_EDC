/**
 * @file    task_test.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   测试任务接口声明。
 * @details
 * 1. TestTask 用于联调阶段的实验性验证，不承载正式业务闭环。
 * 2. 头文件保留常用测试周期与目标参数，便于快速启用测试片段。
 * 3. 所有测试逻辑均应可关闭，避免影响正式任务链路。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_TEST_H
#define FINAL_GRADUATE_WORK_TASK_TEST_H

#include "cmsis_os.h"
#include "mod_motor.h"
#include "mod_sensor.h"
#include "mod_stepper.h"
#include "mod_vofa.h"
#include "pid_config.h"
#include "pid_inc.h"

/* 测试任务循环周期（毫秒） */
#define TASK_TEST_PERIOD_MS (1000000U)
/* Test 任务启动开关：1=正常运行，0=启动后挂起 */
#define TASK_TEST_STARTUP_ENABLE (1U)
/* 预留：测试启动前准备时间（毫秒） */
#define TASK_TEST_PREPARE_MS (5000U)
/* 预留：测试默认目标速度 */
#define TASK_TEST_TARGET_SPEED (40)

/**
 * @brief 测试任务入口。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartTestTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_TEST_H */

