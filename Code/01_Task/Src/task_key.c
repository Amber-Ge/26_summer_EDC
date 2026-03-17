#include "task_key.h"
#include "task_init.h"

/**
 * @brief 底盘模式切换信号量（在 freertos.c 中创建）。
 *
 * @details
 * 由 KEY3 单击事件触发释放，DCC 任务收到后执行控制模式切换。
 */
extern osSemaphoreId_t Sem_TaskChangeHandle;

/**
 * @brief 按键任务入口。
 *
 * @details
 * 任务职责：
 * 1. 周期扫描按键模块事件（mod_key_scan）。
 * 2. 将“输入事件”映射成“业务动作”（释放信号量）。
 *
 * 说明：
 * - 按键底层初始化已迁移到 InitTask，这里只负责事件消费。
 */
void StartKeyTask(void *argument)
{
    (void)argument;

    /* 统一门控，保证按键模块已由 InitTask 完成初始化。 */
    task_wait_init_done();

    for (;;)
    {
        mod_key_event_e evt = mod_key_scan();

        switch (evt)
        {
        case MOD_KEY_EVENT_1_CLICK:
            /* KEY1 单击：通知 GPIO 任务执行灯效动作。 */
            (void)osSemaphoreRelease(Sem_LEDHandle);
            break;

        case MOD_KEY_EVENT_2_CLICK:
            /* KEY2 单击：业务预留。 */
            break;

        case MOD_KEY_EVENT_3_CLICK:
            /* KEY3 单击：通知 DCC 任务切换控制模式。 */
            (void)osSemaphoreRelease(Sem_TaskChangeHandle);
            break;

        case MOD_KEY_EVENT_1_DOUBLE_CLICK:
            /* KEY1 双击：业务预留。 */
            break;

        case MOD_KEY_EVENT_2_DOUBLE_CLICK:
            /* KEY2 双击：业务预留。 */
            break;

        case MOD_KEY_EVENT_3_DOUBLE_CLICK:
            /* KEY3 双击：业务预留。 */
            break;

        case MOD_KEY_EVENT_1_LONG_PRESS:
            /* KEY1 长按：业务预留。 */
            break;

        case MOD_KEY_EVENT_2_LONG_PRESS:
            /* KEY2 长按：业务预留。 */
            break;

        case MOD_KEY_EVENT_3_LONG_PRESS:
            /* KEY3 长按：业务预留。 */
            break;

        case MOD_KEY_EVENT_NONE:
        default:
            /* 无事件或未知事件：不做处理。 */
            break;
        }

        /* 固定扫描周期，保持事件时序稳定。 */
        osDelay(TASK_KEY_PERIOD_MS);
    }
}
