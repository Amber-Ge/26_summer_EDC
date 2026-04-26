/**
 * @file    task_init.h
 * @author  Jiang Kaizhong  Yumeng Ge
 * @version v1.00
 * @date    2026-04-09
 * @brief   InitTask centralized assembly interface.
 * @details
 * 1. InitTask is the only system assembly entry.
 * 2. Board resource binding, module init and startup gating are all exposed here.
 * 3. Other tasks should wait on `task_wait_init_done()` before entering their main loop.
 */
#ifndef ZGT6_FREERTOS_TASK_INIT_H
#define ZGT6_FREERTOS_TASK_INIT_H

#include "cmsis_os.h"

/**
 * @brief InitTask entry function.
 * @details
 * 1. Injects board mapping into default module contexts.
 * 2. Performs one-shot module initialization and startup safe-state writes.
 * 3. Releases the startup gate semaphore and deletes itself.
 * @param argument RTOS task argument, currently unused.
 */
void StartInitTask(void *argument);

/**
 * @brief Wait until InitTask has released the startup gate.
 * @details
 * 1. Blocks while the initialization semaphore is unavailable.
 * 2. Reposts the semaphore after the first pass so later tasks can pass quickly.
 * 3. Intended to be called once before a task enters its main loop.
 */
void task_wait_init_done(void);

#endif /* ZGT6_FREERTOS_TASK_INIT_H */
