#include "task_key.h"
#include "task_init.h"

extern osSemaphoreId_t Sem_TaskChangeHandle;
extern osSemaphoreId_t Sem_ReadyToggleHandle;

void StartKeyTask(void *argument)
{
    (void)argument;

    task_wait_init_done();

    for (;;)
    {
        mod_key_event_e evt = mod_key_scan();
        switch (evt)
        {
        case MOD_KEY_EVENT_1_CLICK:
            (void)osSemaphoreRelease(Sem_RedLEDHandle);
            break;

        case MOD_KEY_EVENT_2_CLICK:
            (void)osSemaphoreRelease(Sem_RedLEDHandle);
            break;

        case MOD_KEY_EVENT_3_CLICK:
            (void)osSemaphoreRelease(Sem_RedLEDHandle);
            (void)osSemaphoreRelease(Sem_TaskChangeHandle);
            break;

        case MOD_KEY_EVENT_3_DOUBLE_CLICK:
            (void)osSemaphoreRelease(Sem_ReadyToggleHandle);
            break;

        case MOD_KEY_EVENT_1_DOUBLE_CLICK:
        case MOD_KEY_EVENT_2_DOUBLE_CLICK:
        case MOD_KEY_EVENT_1_LONG_PRESS:
        case MOD_KEY_EVENT_2_LONG_PRESS:
        case MOD_KEY_EVENT_3_LONG_PRESS:
        case MOD_KEY_EVENT_NONE:
        default:
            break;
        }

        osDelay(TASK_KEY_PERIOD_MS);
    }
}
