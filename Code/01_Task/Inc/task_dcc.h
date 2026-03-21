#ifndef FINAL_GRADUATE_WORK_TASK_DCC_H
#define FINAL_GRADUATE_WORK_TASK_DCC_H

#include "cmsis_os2.h"
#include "mod_motor.h"
#include "mod_sensor.h"
#include "pid_config.h"
#include "pid_inc.h"
#include "pid_pos.h"
#include <stdint.h>

#define TASK_DCC_PERIOD_MS  (20U)
#define TASK_DCC_PREPARE_MS (3000U)

#define TASK_DCC_MODE_IDLE       (0U)
#define TASK_DCC_MODE_STRAIGHT   (1U)
#define TASK_DCC_MODE_TRACK_TODO (2U)
#define TASK_DCC_MODE_TOTAL      (3U)

#define TASK_DCC_RUN_OFF     (0U)
#define TASK_DCC_RUN_PREPARE (1U)
#define TASK_DCC_RUN_ON      (2U)

extern osSemaphoreId_t Sem_TaskChangeHandle;
extern osSemaphoreId_t Sem_ReadyToggleHandle;

void StartDccTask(void *argument);

uint8_t task_dcc_get_mode(void);
uint8_t task_dcc_get_run_state(void);

/* Compatibility API: returns 1 only in TASK_DCC_RUN_ON. */
uint8_t task_dcc_get_ready(void);

#endif /* FINAL_GRADUATE_WORK_TASK_DCC_H */
