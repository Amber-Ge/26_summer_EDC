/**
 ******************************************************************************
 * @file    task_gpio.h
 * @brief   GPIO 业务任务接口定义
 * @details
 * 该任务负责与 GPIO 相关的业务逻辑（如 LED、继电器等）调度。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_LED_H
#define FINAL_GRADUATE_WORK_TASK_LED_H // 头文件防重复包含宏

#include "cmsis_os.h"
#include "mod_led.h"
#include "mod_relay.h"

/** 蜂鸣器单次提示音时长（单位：ms） */
#define TASK_GPIO_BUZZER_BEEP_MS (80U)

/**
 * @brief GPIO 任务入口函数。
 * @details
 * 该函数由 RTOS 任务创建接口调用，内部通常以循环方式等待信号量并执行
 * LED/继电器等 GPIO 相关动作。
 *
 * @param argument 任务参数指针，当前实现中通常未使用。
 */
void StartGpioTask(void *argument);

/**
 * @brief GPIO 任务触发信号量句柄。
 * @details
 * 当其他任务或中断释放该信号量时，GPIO 任务开始执行一次业务动作。
 */
extern osSemaphoreId_t Sem_LEDHandle;

#endif /* FINAL_GRADUATE_WORK_TASK_LED_H */
