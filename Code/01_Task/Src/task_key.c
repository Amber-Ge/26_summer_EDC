/**
 * @file    task_key.c
 * @brief   按键任务实现。
 * @details
 * 1. 文件作用：实现按键事件轮询、分发与任务内响应流程。
 * 2. 上下层绑定：上层由 RTOS 周期调度；下层依赖 `mod_key` 事件输出。
 */
#include "task_key.h"
#include "task_init.h"

/* KEY3单击：切换DCC模式 */
extern osSemaphoreId_t Sem_TaskChangeHandle; // RTOS 信号量句柄，用于任务同步。
/* KEY3双击：请求DCC运行状态切换（OFF/PREPARE/ON） */
extern osSemaphoreId_t Sem_ReadyToggleHandle; // RTOS 信号量句柄，用于任务同步。
/* KEY2单击：请求DCC全重置（mode与运行态全部复位） */
extern osSemaphoreId_t Sem_DccHandle; // RTOS 信号量句柄，用于任务同步。

/* 判断是否为“有按键事件” */
/**
 * @brief 判断扫描结果是否为有效按键事件。
 * @param evt 按键模块输出的事件枚举值。
 * @return 有效事件返回 1，`MOD_KEY_EVENT_NONE` 返回 0。
 */
static int task_key_is_real_event(mod_key_event_e evt)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return (evt != MOD_KEY_EVENT_NONE);
}

/**
 * @brief 按键任务主循环：采集按键事件并转换为 DCC 控制信号量。
 * @param argument 任务参数（未使用）。
 * @return 无。
 */
void StartKeyTask(void *argument)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    mod_key_event_e evt; /* 当前扫描得到的按键事件 */

    (void)argument;

    task_wait_init_done();

    for (;;) // 循环计数器
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

