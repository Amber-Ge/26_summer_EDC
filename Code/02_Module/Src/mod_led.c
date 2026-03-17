#include "mod_led.h"

static GPIO_TypeDef* LED_PORT[LED_MAX] =
{
    LED_RED_GPIO_Port,
    LED_GREEN_GPIO_Port,
    LED_YELLOW_GPIO_Port
}; // LED 端口映射表：逻辑 ID -> GPIO 端口

static uint16_t LED_PIN[LED_MAX] =
{
    LED_RED_Pin,    // RED
    LED_GREEN_Pin,  // GREEN
    LED_YELLOW_Pin  // YELLOW
}; // LED 引脚映射表：逻辑 ID -> GPIO 引脚

/**
 * @brief 初始化 LED (硬件连接为低电平点亮)
 */
void mod_led_Init(void)
{
    //1. 初始化时统一关闭全部 LED，确保上电状态可控
    mod_led_off(LED_RED);
    mod_led_off(LED_GREEN);
    mod_led_off(LED_YELLOW);
}

/**
 * @brief 点亮 LED (硬件连接为低电平点亮)
 */
void mod_led_on(mod_led_id_e led)
{
    //1. 参数校验：非法 LED ID 直接返回
    if (led >= LED_MAX) return;

    //2. 调用驱动层接口输出低电平（本硬件为低电平点亮）
    drv_gpio_write(LED_PORT[led], LED_PIN[led], GPIO_LEVEL_LOW);
}

/**
 * @brief 关闭 LED
 */
void mod_led_off(mod_led_id_e led)
{
    //1. 参数校验：非法 LED ID 直接返回
    if (led >= LED_MAX) return;

    //2. 调用驱动层接口输出高电平（本硬件为高电平熄灭）
    drv_gpio_write(LED_PORT[led], LED_PIN[led], GPIO_LEVEL_HIGH);
}

/**
 * @brief 翻转 LED 状态
 */
void mod_led_toggle(mod_led_id_e led)
{
    //1. 参数校验：非法 LED ID 直接返回
    if (led >= LED_MAX) return;

    //2. 调用驱动层翻转当前 LED 电平状态
    drv_gpio_toggle(LED_PORT[led], LED_PIN[led]);
}
