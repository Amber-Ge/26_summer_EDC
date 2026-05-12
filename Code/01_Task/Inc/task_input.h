/**
 * @file    task_input.h
 * @brief   Input task public interface.
 */
#ifndef ZGT6_FREERTOS_TASK_INPUT_H
#define ZGT6_FREERTOS_TASK_INPUT_H

#include <stdbool.h>
#include <stdint.h>

void StartInputTask(void *argument);
bool task_input_is_lap_select_active(void);
uint8_t task_input_get_selected_laps(void);

#endif /* ZGT6_FREERTOS_TASK_INPUT_H */
