#include "task_key.h"

extern osSemaphoreId_t Sem_TaskChangeHandle; // 底盘模式切换信号量：KEY3 按下后触发

void StartKeyTask(void *argument)
{
    //1. 初始化按键驱动内部状态机
    drv_key_init();
    (void)argument; // 当前任务参数未使用

    //2. 主循环：固定周期扫描按键事件并分发业务
    for (;;)
    {
        DrvKeyEvent_e evt; // 本轮扫描得到的按键事件

        //2.1 扫描一次按键，返回“确认按下”事件
        evt = drv_key_scan();

        //2.2 根据事件执行对应业务动作
        switch(evt)
        {
        case DRV_KEY_EVENT_1_PRESSED:
            {
                osSemaphoreRelease(Sem_LEDHandle);
                // KEY1 按下事件处理入口
                break;
            }

        case DRV_KEY_EVENT_2_PRESSED:
            {
                // KEY2 按下事件处理入口
                break;
            }

        case DRV_KEY_EVENT_3_PRESSED:
            {
                // KEY3：触发底盘任务模式切换（0<->1）
                osSemaphoreRelease(Sem_TaskChangeHandle);
                break;
            }

        default:
            {
                // 无事件，不执行额外动作
                break;
            }
        }

        //2.3 固定周期延时，保持驱动消抖时基稳定
        osDelay(TASK_KEY_PERIOD_MS);
    }
}
