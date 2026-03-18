#include "task_key.h"
#include "task_init.h"

/**
 * @brief DCC 模式切换信号量（由 freertos.c 创建）
 *
 * @details
 * KEY3 单击时释放该信号量，DCC 任务收到后切换 mode。
 */
extern osSemaphoreId_t Sem_TaskChangeHandle;

/**
 * @brief DCC ready 切换信号量（由 freertos.c 创建）
 *
 * @details
 * KEY3 双击时释放该信号量，DCC 任务收到后执行 ready 取反。
 */
extern osSemaphoreId_t Sem_ReadyToggleHandle;

/**
 * @brief 按键任务入口函数
 * @param argument 任务参数，当前未使用
 *
 * @details
 * 本任务只负责“按键事件 -> 信号量通知”的转换，不直接修改 DCC 的 mode/ready 变量。
 */
void StartKeyTask(void *argument)
{
    (void)argument; // 显式忽略未使用参数，避免编译告警

    // 等待 InitTask 完成全局初始化，防止按键任务早于模块初始化开始运行
    task_wait_init_done();

    for (;;)
    {
        mod_key_event_e evt = mod_key_scan(); // 当前扫描周期得到的按键事件

        switch (evt)
        {
        case MOD_KEY_EVENT_1_CLICK:
            // KEY1 单击：通知 GPIO 任务执行原有灯效动作
            (void)osSemaphoreRelease(Sem_LEDHandle);
            break;

        case MOD_KEY_EVENT_2_CLICK:
            // KEY2 单击：业务预留
            break;

        case MOD_KEY_EVENT_3_CLICK:
            // KEY3 单击：通知 DCC 切换模式（mode 循环）
            (void)osSemaphoreRelease(Sem_TaskChangeHandle);
            break;

        case MOD_KEY_EVENT_1_DOUBLE_CLICK:
            // KEY1 双击：业务预留
            break;

        case MOD_KEY_EVENT_2_DOUBLE_CLICK:
            // KEY2 双击：业务预留
            break;

        case MOD_KEY_EVENT_3_DOUBLE_CLICK:
            // KEY3 双击：通知 DCC 切换 ready（0/1 取反）
            (void)osSemaphoreRelease(Sem_ReadyToggleHandle);
            break;

        case MOD_KEY_EVENT_1_LONG_PRESS:
            // KEY1 长按：业务预留
            break;

        case MOD_KEY_EVENT_2_LONG_PRESS:
            // KEY2 长按：业务预留
            break;

        case MOD_KEY_EVENT_3_LONG_PRESS:
            // KEY3 长按：业务预留
            break;

        case MOD_KEY_EVENT_NONE:
        default:
            // 无事件：本周期不执行任何动作
            break;
        }

        // 固定扫描周期，保证按键时序与消抖判定稳定
        osDelay(TASK_KEY_PERIOD_MS);
    }
}
