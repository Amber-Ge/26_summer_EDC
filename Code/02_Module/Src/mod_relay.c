#include "mod_relay.h"

static GPIO_TypeDef *RELAY_PORT[RELAY_MAX] =
{
    Laser_GPIO_Port,
}; // 继电器端口映射表：逻辑 ID -> GPIO 端口

static uint16_t RELAY_PIN[RELAY_MAX] =
{
    Laser_Pin,
}; // 继电器引脚映射表：逻辑 ID -> GPIO 引脚

void mod_relay_init(void)
{
    //1. 初始化时关闭所有继电器通道，确保上电安全状态
    mod_relay_off(RELAY_LASER);
}

void mod_relay_on(mod_relay_id_e relay)
{
    //1. 参数校验：非法继电器 ID 直接返回
    if (relay >= RELAY_MAX) return;

    //2. 继电器层定义：高电平为吸合（ON）
    drv_gpio_write(RELAY_PORT[relay], RELAY_PIN[relay], GPIO_LEVEL_HIGH);
}

void mod_relay_off(mod_relay_id_e relay)
{
    //1. 参数校验：非法继电器 ID 直接返回
    if (relay >= RELAY_MAX) return;

    //2. 继电器层定义：低电平为断开（OFF）
    drv_gpio_write(RELAY_PORT[relay], RELAY_PIN[relay], GPIO_LEVEL_LOW);
}

void mod_relay_toggle(mod_relay_id_e relay)
{
    //1. 参数校验：非法继电器 ID 直接返回
    if (relay >= RELAY_MAX) return;

    //2. 翻转当前继电器通道输出电平
    drv_gpio_toggle(RELAY_PORT[relay], RELAY_PIN[relay]);
}
