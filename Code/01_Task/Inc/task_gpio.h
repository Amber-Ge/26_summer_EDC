/**
 * @file    task_gpio.h
 * @brief   GPIO 任务接口声明。
 * @details
 * 1. 文件作用：定义 GPIO 任务节拍参数和任务入口，统一管理灯光/继电器反馈策略。
 * 2. 上层绑定：`KeyTask` 通过 `Sem_RedLEDHandle` 注入按键反馈事件，`GpioTask` 周期消费并驱动反馈输出。
 * 3. 下层依赖：`mod_led` 与 `mod_relay` 模块，输出红/绿/黄灯、蜂鸣器和激光继电器状态。
 * 4. 生命周期：`GpioTask` 常驻运行，每个周期依据 DCC 运行态更新外设输出。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_LED_H
#define FINAL_GRADUATE_WORK_TASK_LED_H

#include "cmsis_os.h"
#include "mod_led.h"
#include "mod_relay.h"

/* GPIO任务调度周期 */
#define TASK_GPIO_PERIOD_MS (20U)

/* 任意按键反馈：黄灯脉冲时长 */
#define TASK_GPIO_KEY_FLASH_MS (120U)
/* 任意按键反馈：蜂鸣器短鸣时长 */
#define TASK_GPIO_KEY_BEEP_MS (90U)

/* ON态：绿灯闪烁周期 */
#define TASK_GPIO_ON_GREEN_BLINK_MS (300U)

/* STOP态：红灯闪烁周期 */
#define TASK_GPIO_STOP_RED_BLINK_MS (250U)

/* STOP态蜂鸣节奏：短响 + 停顿 */
#define TASK_GPIO_STOP_BUZZER_ON_MS (90U)
#define TASK_GPIO_STOP_BUZZER_OFF_MS (210U)

void StartGpioTask(void *argument);

/* 按键反馈信号量：由 KeyTask 释放，GpioTask 消费。 */
extern osSemaphoreId_t Sem_RedLEDHandle; // RTOS 信号量句柄，用于任务同步。

#endif /* FINAL_GRADUATE_WORK_TASK_LED_H */

