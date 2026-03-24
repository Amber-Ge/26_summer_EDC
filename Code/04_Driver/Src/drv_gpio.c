/**
 * @file    drv_gpio.c
 * @brief   GPIO 驱动层统一接口实现。
 * @details
 * 1. 文件作用：完成逻辑电平与 HAL 引脚电平间的双向转换与基础引脚操作。
 * 2. 解耦边界：不理解设备语义，仅提供原子读写/翻转能力。
 * 3. 上层绑定：LED、继电器、传感器、按键等模块复用本文件接口。
 * 4. 下层依赖：HAL_GPIO_WritePin/HAL_GPIO_ReadPin/HAL_GPIO_TogglePin。
 */

#include "drv_gpio.h"

/**
 * @brief 写入指定 GPIO 引脚逻辑电平。
 * @param GPIOx GPIO 端口（如 `GPIOA`）。
 * @param GPIO_Pin GPIO 引脚掩码（如 `GPIO_PIN_5`）。
 * @param level 目标逻辑电平。
 * @return 无返回值。
 */
void drv_gpio_write(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, gpio_level_e level)
{
    GPIO_PinState pin_state = (level == GPIO_LEVEL_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET; // HAL 层目标引脚电平

    // 1. 将驱动层逻辑电平枚举映射为 HAL 层引脚电平类型。
    // 2. 调用 HAL 接口写入目标 GPIO 引脚。
    HAL_GPIO_WritePin(GPIOx, GPIO_Pin, pin_state);
}

/**
 * @brief 读取指定 GPIO 引脚逻辑电平。
 * @param GPIOx GPIO 端口。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @return gpio_level_e 当前引脚逻辑电平。
 */
gpio_level_e drv_gpio_read(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(GPIOx, GPIO_Pin); // HAL 层采样到的引脚电平

    // 1. 从 HAL 层读取当前 GPIO 引脚电平。
    // 2. 将 HAL 电平转换为驱动层统一逻辑电平后返回。
    return (pin_state == GPIO_PIN_SET) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

/**
 * @brief 翻转指定 GPIO 引脚电平。
 * @param GPIOx GPIO 端口。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @return 无返回值。
 */
void drv_gpio_toggle(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    // 1. 调用 HAL 接口翻转目标 GPIO 引脚输出电平。
    HAL_GPIO_TogglePin(GPIOx, GPIO_Pin);
}
