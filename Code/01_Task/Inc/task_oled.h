/**
 ******************************************************************************
 * @file    task_oled.h
 * @brief   OLED 显示任务接口定义
 * @details
 * 该任务负责周期刷新 OLED 内容，并按设定周期更新电池采样数据显示。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_OLED_H
#define FINAL_GRADUATE_WORK_TASK_OLED_H

#include "mod_battery.h"
#include "mod_oled.h"
#include "cmsis_os2.h"

/** OLED 刷新周期（单位：毫秒） */
#define TASK_OLED_REFRESH_MS (500U) // 0.5 秒刷新一次

/** 电池采样周期（单位：秒） */
#define TASK_OLED_SAMPLE_S (10U)

/** OLED 任务空闲让出 CPU 的延时 Tick */
#define TASK_OLED_IDLE_DELAY_TICK (50U)

/**
 * @brief OLED 任务入口函数
 * @param argument 任务参数指针（当前实现中通常未使用）
 */
void StartOledTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_OLED_H */
