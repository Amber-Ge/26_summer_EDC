/**
 * @file    task_gimbal.c
 * @author  Amber Ge
 * @brief   云台任务：按固定周期读取统一视觉状态。
 */

#include "task_gimbal.h"

#include "cmsis_os.h"
#include "task_init.h"

#include "mod_vision.h"

#define TASK_GIMBAL_PERIOD_MS (10U)

void StartGimbalTask(void *argument)
{
    mod_vision_ctx_t *vision_ctx;
    mod_vision_data_t vision_data;

    (void)argument;

    task_wait_init_done();
    vision_ctx = mod_vision_get_default_ctx();

    for (;;)
    {
        (void)mod_vision_get_latest_data(vision_ctx, &vision_data);
        /* 后续云台状态机、步进控制和保护逻辑都基于这份统一视觉状态展开。 */
        osDelay(TASK_GIMBAL_PERIOD_MS);
    }
}
