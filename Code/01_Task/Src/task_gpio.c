#include "task_gpio.h"

/* LED硬件绑定表：任务启动时显式绑定到模块层 */
static const mod_led_hw_cfg_t s_led_bind_map[LED_MAX] =
{
    [LED_RED] = {
        .port = LED_RED_GPIO_Port,
        .pin = LED_RED_Pin,
        .active_level = GPIO_LEVEL_LOW,
    },
    [LED_GREEN] = {
        .port = LED_GREEN_GPIO_Port,
        .pin = LED_GREEN_Pin,
        .active_level = GPIO_LEVEL_LOW,
    },
    [LED_YELLOW] = {
        .port = LED_YELLOW_GPIO_Port,
        .pin = LED_YELLOW_Pin,
        .active_level = GPIO_LEVEL_LOW,
    },
};

/* 继电器硬件绑定表：任务启动时显式绑定到模块层 */
static const mod_relay_hw_cfg_t s_relay_bind_map[RELAY_MAX] =
{
    [RELAY_LASER] = {
        .port = Laser_GPIO_Port,
        .pin = Laser_Pin,
        .active_level = GPIO_LEVEL_HIGH,
    },
};

void StartGpioTask(void *argument)
{
    (void)argument;

    // 1. 显式绑定LED映射（无默认回落）。
    (void)mod_led_bind_map(s_led_bind_map, LED_MAX);

    // 2. 显式绑定继电器映射（无默认回落）。
    (void)mod_relay_bind_map(s_relay_bind_map, RELAY_MAX);

    // 3. 初始化模块，确保上电状态可控。
    mod_led_Init();
    mod_relay_init();

    // 4. 主循环：等待触发信号并执行动作。
    for (;;)
    {
        // 4.1 阻塞等待触发信号。
        osSemaphoreAcquire(Sem_LEDHandle, osWaitForever);

        // 4.2 执行动作序列：红灯翻转 -> 延时 -> 绿灯翻转。
        mod_led_toggle(LED_RED);
        osDelay(500U);
        mod_led_toggle(LED_GREEN);
    }
}