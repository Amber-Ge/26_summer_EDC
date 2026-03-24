/**
 * @file    task_key.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   按键任务实现。
 * @details
 * 1. KeyTask 周期调用 mod_key_scan 获取去抖后的按键事件。
 * 2. 任意有效事件先触发 GPIO 反馈信号量，再按事件类型映射业务动作。
 * 3. 任务只负责“事件分发”，不直接改写 DCC 内部状态。
 */

#include "task_key.h"

#include "task_init.h"

/* KEY3 单击：切换 DCC 模式 */
extern osSemaphoreId_t Sem_TaskChangeHandle;
/* KEY3 双击：切换 DCC 运行态 */
extern osSemaphoreId_t Sem_ReadyToggleHandle;
/* KEY2 单击：DCC 全重置 */
extern osSemaphoreId_t Sem_DccHandle;

/**
 * @brief 判断扫描结果是否为有效按键事件。
 * @param evt 模块层返回的按键事件枚举。
 * @return 有效事件返回 1；无事件（MOD_KEY_EVENT_NONE）返回 0。
 */
static int task_key_is_real_event(mod_key_event_e evt)
{
    return (evt != MOD_KEY_EVENT_NONE);
}

/**
 * @brief KeyTask 主循环：扫描按键并分发信号量。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartKeyTask(void *argument)
{
    mod_key_event_e evt = MOD_KEY_EVENT_NONE;         /* 当前扫描得到的按键事件 */
    mod_key_ctx_t *key_ctx = mod_key_get_default_ctx(); /* Key 模块默认上下文 */

    (void)argument;

    /* 等待 InitTask 完成模块绑定，避免在未初始化上下文上扫描。 */
    task_wait_init_done();

#if (TASK_KEY_STARTUP_ENABLE == 0U)
    /* 启动开关关闭：挂起当前任务，保留线程对象便于后续手动恢复。 */
    (void)osThreadSuspend(osThreadGetId());
    for (;;)
    {
        osDelay(osWaitForever);
    }
#endif

    for (;;)
    {
        /* 步骤1：执行一次周期扫描并获取去抖后的事件类型。 */
        evt = mod_key_scan(key_ctx);

        /* 任意有效按键先触发黄灯/短鸣反馈。 */
        if (task_key_is_real_event(evt) != 0)
        {
            (void)osSemaphoreRelease(Sem_RedLEDHandle);
        }

        /* 步骤2：事件映射：按键事件 -> DCC 控制信号量。 */
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
            /* 其余事件当前不驱动 DCC，仅保留通用反馈。 */
            break;
        }

        /* 步骤3：周期让出 CPU，稳定扫描节奏。 */
        osDelay(TASK_KEY_PERIOD_MS);
    }
}

