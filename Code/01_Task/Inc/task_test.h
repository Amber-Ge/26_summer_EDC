/**
 ******************************************************************************
 * @file    task_test.h
 * @brief   测试任务接口定义
 * @details
 * 该任务用于联调阶段的传感器、运动控制与 PID 参数验证。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_CONTROL_H
#define FINAL_GRADUATE_WORK_TASK_CONTROL_H // 头文件防重复包含宏

#include "cmsis_os.h"
#include "mod_sensor.h"
#include "mod_vofa.h"
#include "mod_motor.h"
#include "mod_stepper.h"
#include "pid_config.h"
#include "pid_inc.h"

/** 测试任务运行周期，单位 ms */
#define TASK_TEST_PERIOD_MS      (20U) // 测试任务运行周期，单位 ms
/** 测试任务启动前预留准备时间，单位 ms */
#define TASK_TEST_PREPARE_MS     (5000U) // 测试任务启动前准备时间，单位 ms
/** 默认测试目标速度 */
#define TASK_TEST_TARGET_SPEED   (40) // 默认测试目标速度

/**
 * @brief 测试任务入口函数。
 * @details
 * 由 RTOS 创建后运行，用于执行测试流程与数据上报。
 *
 * @param argument 任务参数指针，当前实现中通常未使用。
 */
void StartTestTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_CONTROL_H */
