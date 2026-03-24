/**
 * @file    task_gpio.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   GPIO 反馈任务接口声明。
 * @details
 * 1. 该任务统一管理红/绿/黄灯、蜂鸣器继电器和激光继电器输出。
 * 2. 输入事件来自 DCC 运行状态和 KeyTask 释放的按键反馈信号量。
 * 3. 任务只调用 mod_led/mod_relay，不直接访问 HAL GPIO。
 */
#ifndef FINAL_GRADUATE_WORK_TASK_GPIO_H
#define FINAL_GRADUATE_WORK_TASK_GPIO_H

#include "cmsis_os.h"
#include "mod_led.h"
#include "mod_relay.h"

/* GPIO 任务主循环周期（毫秒） */
#define TASK_GPIO_PERIOD_MS (20U)
/* GPIO 任务启动开关：1=正常运行，0=启动后挂起 */
#define TASK_GPIO_STARTUP_ENABLE (1U)

/* 按键反馈黄灯脉冲持续时间（毫秒） */
#define TASK_GPIO_KEY_FLASH_MS (120U)
/* 按键反馈蜂鸣短鸣持续时间（毫秒） */
#define TASK_GPIO_KEY_BEEP_MS (90U)

/* DCC ON 态绿灯闪烁周期（毫秒） */
#define TASK_GPIO_ON_GREEN_BLINK_MS (300U)
/* DCC STOP 态红灯闪烁周期（毫秒） */
#define TASK_GPIO_STOP_RED_BLINK_MS (250U)

/* DCC STOP 态蜂鸣节奏：鸣叫时长（毫秒） */
#define TASK_GPIO_STOP_BUZZER_ON_MS (90U)
/* DCC STOP 态蜂鸣节奏：静默时长（毫秒） */
#define TASK_GPIO_STOP_BUZZER_OFF_MS (210U)

/* 任意按键事件反馈信号量：KeyTask 释放，GpioTask 消费 */
extern osSemaphoreId_t Sem_RedLEDHandle;

/**
 * @brief GPIO 反馈任务入口。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartGpioTask(void *argument);

#endif /* FINAL_GRADUATE_WORK_TASK_GPIO_H */

