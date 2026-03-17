#include "drv_gpio.h"

void drv_gpio_write(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin, gpio_level_e level)
{
    GPIO_PinState pin_state = (level == GPIO_LEVEL_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET; // HAL 层引脚电平值

    //1. 将驱动层逻辑电平枚举映射到 HAL 的 GPIO_PinState
    //2. 调用 HAL 接口写入目标引脚
    HAL_GPIO_WritePin(GPIOx, GPIO_Pin, pin_state);
}

gpio_level_e drv_gpio_read(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin)
{
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(GPIOx, GPIO_Pin); // HAL 读取到的物理引脚电平

    //1. 读取硬件引脚当前电平
    //2. 转换为驱动层统一的逻辑电平枚举并返回
    return (pin_state == GPIO_PIN_SET) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

void drv_gpio_toggle(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin)
{
    //1. 调用 HAL 翻转指定引脚输出状态
    HAL_GPIO_TogglePin(GPIOx, GPIO_Pin);
}
