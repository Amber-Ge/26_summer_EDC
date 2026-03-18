#include "task_gpio.h"
#include "task_init.h"

/**
 * @brief GPIO 业务任务入口函数。
 *
 * @details
 * 1. 本任务只处理 GPIO 业务动作，不负责硬件初始化与绑定。
 * 2. 所有硬件绑定统一在 InitTask 完成，本任务启动后先等待初始化闸门放行。
 * 3. 收到 Sem_LED 信号量后，执行一组可视化/提示动作（LED + 蜂鸣器）。
 *
 * @param argument 任务参数，当前实现未使用，保留是为了匹配 RTOS 任务入口签名。
 */
void StartGpioTask(void *argument)
{
    (void)argument; // 显式说明 argument 未使用，避免编译器告警。

    // 等待 InitTask 完成全局初始化，确保 LED/Relay 已经绑定到有效硬件资源。
    task_wait_init_done();

    for (;;)
    {
        // 阻塞等待业务触发信号；信号量由其他任务（例如按键任务）释放。
        (void)osSemaphoreAcquire(Sem_LEDHandle, osWaitForever);

        // 第一步：切换红灯，给出动作开始提示。
        mod_led_toggle(LED_RED);
        // 固定延时 500ms，用于形成可见的 LED 节奏。
        osDelay(500U);
        // 第二步：切换绿灯，形成一组完整灯光反馈。
        mod_led_toggle(LED_GREEN);

        // 第三步：打开蜂鸣器（作为 relay 模块中的一个逻辑 ID，不新增专用函数）。
        mod_relay_on(RELAY_BUZZER);
        // 保持短促提示音，时长由任务层宏统一配置，便于后续集中调整。
        osDelay(TASK_GPIO_BUZZER_BEEP_MS);
        // 第四步：关闭蜂鸣器，避免持续鸣叫影响后续业务。
        mod_relay_off(RELAY_BUZZER);
    }
}
