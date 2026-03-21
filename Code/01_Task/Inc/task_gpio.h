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

#define TASK_GPIO_LED_PULSE_MS    (80U)
#define TASK_GPIO_BUZZER_BEEP_MS  (80U)

void StartGpioTask(void *argument);

/* Key click feedback semaphore. */
extern osSemaphoreId_t Sem_RedLEDHandle;

#endif /* FINAL_GRADUATE_WORK_TASK_LED_H */
