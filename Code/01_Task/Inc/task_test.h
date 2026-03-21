/**
 ******************************************************************************
 * @file    task_test.h
 * @brief   测试任务接口定义
 * @details
 * 当前用于周期读取 12 路循迹状态并通过 VOFA 上报。
 ******************************************************************************
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

#define TASK_TEST_PERIOD_MS    (1000U)  // 测试任务采样/发送周期（ms）
#define TASK_TEST_PREPARE_MS   (5000U)  // 预留：测试启动前准备时间（ms）
#define TASK_TEST_TARGET_SPEED (40)     // 预留：测试默认目标速度

void StartTestTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_CONTROL_H */
