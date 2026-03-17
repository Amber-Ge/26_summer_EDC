#include "task_gpio.h"
#include "task_init.h"

/**
 * @brief GPIO 业务任务入口。
 *
 * @details
 * 本任务已经不再承担硬件绑定与初始化职责（已解耦到 InitTask）：
 * 1. 启动后先等待全局初始化完成。
 * 2. 再进入“等待信号量 -> 执行动作序列”的业务循环。
 *
 * 当前动作序列：
 * - 收到 Sem_LED 后，翻转红灯 -> 延时 500ms -> 翻转绿灯。
 */
void StartGpioTask(void *argument)
{
    (void)argument;

    /* 统一启动门控：确保 LED/Relay 模块已在 InitTask 中完成初始化。 */
    task_wait_init_done();

    for (;;)
    {
        /* 阻塞等待外部触发（例如按键任务释放 Sem_LED）。 */
        (void)osSemaphoreAcquire(Sem_LEDHandle, osWaitForever);

        /* 执行一次可视化动作序列。 */
        mod_led_toggle(LED_RED);
        osDelay(500U);
        mod_led_toggle(LED_GREEN);
    }
}
