/**
 * @file    task_oled.h
 * @brief   OLED 显示任务接口声明。
 * @details
 * 1. 文件作用：定义 OLED 刷新/采样节拍参数并声明任务入口。
 * 2. 上层绑定：由 RTOS 周期调度，读取 DCC 状态后输出到显示层。
 * 3. 下层依赖：`mod_oled` 负责显示驱动，`mod_battery` 提供电压采样值。
 * 4. 生命周期：任务常驻，执行“采样更新 + 页面刷新”双周期流程。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_OLED_H
#define FINAL_GRADUATE_WORK_TASK_OLED_H

#include "mod_battery.h"
#include "mod_oled.h"
#include "cmsis_os2.h"

/** OLED 刷新周期（毫秒） */
#define TASK_OLED_REFRESH_MS (500U) // 0.5 秒刷新一次

/** 电池采样周期（秒） */
#define TASK_OLED_SAMPLE_S (10U)

/** OLED 任务空闲让出 CPU 的延时 Tick */
#define TASK_OLED_IDLE_DELAY_TICK (50U)

/**
 * @brief OLED 任务入口函数。
 * @param argument 任务参数指针（当前实现未使用）。
 */
void StartOledTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_OLED_H */
