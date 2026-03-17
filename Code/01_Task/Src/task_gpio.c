#include "task_gpio.h"

void StartGpioTask(void *argument)
{
    //1. 初始化 GPIO 相关模块，确保 LED 与继电器处于已知状态
    mod_led_Init();
    mod_relay_init();

    //2. 进入任务主循环，等待事件后执行灯光动作
    for (;;)
    {
        //2.1 阻塞等待触发信号量，收到信号后执行一次动作序列
        osSemaphoreAcquire(Sem_LEDHandle, osWaitForever);

        //2.2 执行动作序列：红灯翻转 -> 延时 -> 绿灯翻转
        mod_led_toggle(LED_RED);
        osDelay(500);
        mod_led_toggle(LED_GREEN);
    }
}
