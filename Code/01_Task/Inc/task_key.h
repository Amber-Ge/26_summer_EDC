/**
 ******************************************************************************
 * @file    task_key.h
 * @brief   Key task interfaces
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_KEY_H
#define FINAL_GRADUATE_WORK_TASK_KEY_H

#include "cmsis_os.h"
#include "mod_key.h"

#define TASK_KEY_PERIOD_MS (MOD_KEY_SCAN_PERIOD_MS)

void StartKeyTask(void *argument);

/* Any key click feedback semaphore. */
extern osSemaphoreId_t Sem_RedLEDHandle;

#endif /* FINAL_GRADUATE_WORK_TASK_KEY_H */
