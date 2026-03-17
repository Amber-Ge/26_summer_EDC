#include "task_key.h"

extern osSemaphoreId_t Sem_TaskChangeHandle; // 底盘模式切换信号量：KEY3 单击后触发

void StartKeyTask(void *argument)
{
    // 1. 显式声明任务参数未使用，避免编译器警告。
    (void)argument;

    // 2. 初始化按键模块（模块内部会完成驱动初始化与引脚绑定）。
    mod_key_init();

    // 3. 进入任务主循环，按固定周期扫描按键。
    for (;;)
    {
        mod_key_event_e evt; // 保存本轮扫描到的按键事件

        // 3.1 从模块层获取本轮事件。
        evt = mod_key_scan();

        // 3.2 根据事件做业务分发。
        switch (evt)
        {
        case MOD_KEY_EVENT_1_CLICK:
            // 3.2.1 KEY1 单击：触发 GPIO 任务。
            osSemaphoreRelease(Sem_LEDHandle);
            break;

        case MOD_KEY_EVENT_2_CLICK:
            // 3.2.2 KEY2 单击：当前预留，不执行动作。
            break;

        case MOD_KEY_EVENT_3_CLICK:
            // 3.2.3 KEY3 单击：触发底盘模式切换。
            osSemaphoreRelease(Sem_TaskChangeHandle);
            break;

        case MOD_KEY_EVENT_1_DOUBLE_CLICK:
            // 3.2.4 KEY1 双击：当前预留，不执行动作。

            break;

        case MOD_KEY_EVENT_2_DOUBLE_CLICK:
            // 3.2.5 KEY2 双击：当前预留，不执行动作。
            break;

        case MOD_KEY_EVENT_3_DOUBLE_CLICK:
            // 3.2.6 KEY3 双击：当前预留，不执行动作。
            break;

        case MOD_KEY_EVENT_1_LONG_PRESS:
            // 3.2.7 KEY1 长按：当前预留，不执行动作。

            break;

        case MOD_KEY_EVENT_2_LONG_PRESS:
            // 3.2.8 KEY2 长按：当前预留，不执行动作。
            break;

        case MOD_KEY_EVENT_3_LONG_PRESS:
            // 3.2.9 KEY3 长按：当前预留，不执行动作。
            break;

        case MOD_KEY_EVENT_NONE:
        default:
            // 3.2.10 无事件或异常事件：不执行动作。
            break;
        }

        // 3.3 固定周期延时，保持扫描时基稳定。
        osDelay(TASK_KEY_PERIOD_MS);
    }
}
