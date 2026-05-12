/**
 * @file    task_debug.h
 * @brief   Debug task public interface.
 */
#ifndef ZGT6_FREERTOS_TASK_DEBUG_H
#define ZGT6_FREERTOS_TASK_DEBUG_H

#include <stdbool.h>

typedef enum
{
    TASK_DEBUG_MODE_NONE = 0,
    TASK_DEBUG_MODE_PID_TUNE_UART,
    TASK_DEBUG_MODE_MAX
} task_debug_mode_t;

void StartDebugTask(void *argument);
bool task_debug_set_mode(task_debug_mode_t mode);
task_debug_mode_t task_debug_get_mode(void);

#endif /* ZGT6_FREERTOS_TASK_DEBUG_H */
