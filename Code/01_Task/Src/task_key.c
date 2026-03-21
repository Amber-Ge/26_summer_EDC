#include "task_key.h"
#include "task_init.h"

/* KEY3单击：切换DCC模式 */
extern osSemaphoreId_t Sem_TaskChangeHandle;
/* KEY3双击：请求DCC运行状态切换（OFF/PREPARE/ON） */
extern osSemaphoreId_t Sem_ReadyToggleHandle;
/* KEY2单击：请求DCC全重置（mode与运行态全部复位） */
extern osSemaphoreId_t Sem_DccHandle;

/* 判断是否为“有按键事件” */
static int task_key_is_real_event(mod_key_event_e evt)
{
    return (evt != MOD_KEY_EVENT_NONE);
}

void StartKeyTask(void *argument)
{
    mod_key_event_e evt; /* 当前扫描得到的按键事件 */

    (void)argument;

    task_wait_init_done();

    for (;;)
    {
        evt = mod_key_scan();

        /* 任意按键事件都触发一次黄灯反馈（GPIO任务消费该信号量） */
        if (task_key_is_real_event(evt) != 0)
        {
            (void)osSemaphoreRelease(Sem_RedLEDHandle);
        }

        /* 业务按键映射：只使用KEY3控制DCC */
        switch (evt)
        {
        case MOD_KEY_EVENT_2_CLICK:
            (void)osSemaphoreRelease(Sem_DccHandle);
            break;

        case MOD_KEY_EVENT_3_CLICK:
            (void)osSemaphoreRelease(Sem_TaskChangeHandle);
            break;

        case MOD_KEY_EVENT_3_DOUBLE_CLICK:
            (void)osSemaphoreRelease(Sem_ReadyToggleHandle);
            break;

        case MOD_KEY_EVENT_1_CLICK:
        case MOD_KEY_EVENT_1_DOUBLE_CLICK:
        case MOD_KEY_EVENT_2_DOUBLE_CLICK:
        case MOD_KEY_EVENT_1_LONG_PRESS:
        case MOD_KEY_EVENT_2_LONG_PRESS:
        case MOD_KEY_EVENT_3_LONG_PRESS:
        case MOD_KEY_EVENT_NONE:
        default:
            /* 其他按键仅保留黄灯反馈，不绑定DCC控制动作 */
            break;
        }

        osDelay(TASK_KEY_PERIOD_MS);
    }
}
