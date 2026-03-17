/**
 ******************************************************************************
 * @file    task_oled.h
 * @brief   OLED 显示任务接口定义
 * @details
 * 该任务负责周期性刷新 OLED 内容，并按设定周期更新电池采样数据显示。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_OLED_H
#define FINAL_GRADUATE_WORK_TASK_OLED_H // 头文件防重复包含宏

#include "mod_battery.h"
#include "mod_oled.h"
#include "cmsis_os2.h"

/** OLED 刷新周期，单位 s */
#define TASK_OLED_REFRESH_S       (1U) // OLED 刷新周期，单位 s
/** 电池采样周期，单位 s */
#define TASK_OLED_SAMPLE_S        (10U) // 电池采样周期，单位 s
/** 任务空闲延时 Tick */
#define TASK_OLED_IDLE_DELAY_TICK (200U) // OLED 任务空闲延时 Tick

/**
 * @brief OLED 任务入口函数。
 * @details
 * 由 RTOS 线程创建接口调用，内部执行显示刷新与电池数据采样调度。
 *
 * @param argument 任务参数指针，当前实现中通常未使用。
 */
void StartOledTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_OLED_H */
