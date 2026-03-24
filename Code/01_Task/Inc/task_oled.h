/**
 * @file    task_oled.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   OLED 显示任务接口声明。
 * @details
 * 1. 该任务负责周期刷新页面并显示电压、DCC 模式和运行状态。
 * 2. 电压数据来自 mod_battery 缓存，显示驱动来自 mod_oled。
 * 3. 任务内部采用采样周期与刷新周期双时基，避免无效刷新。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_OLED_H
#define FINAL_GRADUATE_WORK_TASK_OLED_H

#include "cmsis_os2.h"
#include "mod_battery.h"
#include "mod_oled.h"

/* OLED 页面刷新周期（毫秒） */
#define TASK_OLED_REFRESH_MS (500U)
/* OLED 任务启动开关：1=正常运行，0=启动后挂起 */
#define TASK_OLED_STARTUP_ENABLE (1U)
/* 电池电压采样周期（秒） */
#define TASK_OLED_SAMPLE_S (10U)
/* OLED 任务空闲让出 CPU 的延时 tick */
#define TASK_OLED_IDLE_DELAY_TICK (50U)

/**
 * @brief OLED 任务入口。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartOledTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_OLED_H */

