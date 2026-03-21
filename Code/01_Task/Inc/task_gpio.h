/**
 ******************************************************************************
 * @file    task_gpio.h
 * @brief   GPIO task interfaces
 ******************************************************************************
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

/* 按键反馈信号量：由KeyTask释放，GpioTask消费。 */
extern osSemaphoreId_t Sem_RedLEDHandle;

#endif /* FINAL_GRADUATE_WORK_TASK_LED_H */
