/**
 * @file    drv_gpio.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   GPIO 驱动统一接口实现。
 * @details
 * 1. 本文件实现逻辑电平与 HAL 电平之间的双向映射。
 * 2. 本文件仅处理引脚原子访问，不参与任何业务状态决策。
 * 3. 上层模块通过本文件复用 GPIO 操作，避免直接散落 HAL 调用。
 */

#include "drv_gpio.h"

/**
 * @brief 向指定引脚写入逻辑电平。
 * @param GPIOx GPIO 端口句柄。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @param level 目标逻辑电平。
 */
void drv_gpio_write(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, gpio_level_e level)
{
    GPIO_PinState pin_state = GPIO_PIN_RESET; // pin_state：HAL 层电平枚举值。

    // 步骤1：端口句柄保护，避免非法访问。
    if (GPIOx == NULL)
    {
        return;
    }

    // 步骤2：将驱动层逻辑电平映射为 HAL 电平。
    pin_state = (level == GPIO_LEVEL_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    // 步骤3：写入目标引脚。
    HAL_GPIO_WritePin(GPIOx, GPIO_Pin, pin_state);
}

void drv_gpio_write_pin(const drv_gpio_pin_t *pin, gpio_level_e level)
{
    if (pin == NULL)
    {
        return;
    }

    drv_gpio_write((GPIO_TypeDef *)pin->port, (uint16_t)pin->pin, level);
}

/**
 * @brief 读取指定引脚逻辑电平。
 * @param GPIOx GPIO 端口句柄。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @return gpio_level_e 当前逻辑电平。
 */
gpio_level_e drv_gpio_read(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    GPIO_PinState pin_state = GPIO_PIN_RESET; // pin_state：HAL 返回的原始电平。

    // 步骤1：端口句柄保护，非法参数默认返回低电平。
    if (GPIOx == NULL)
    {
        return GPIO_LEVEL_LOW;
    }

    // 步骤2：读取 HAL 电平。
    pin_state = HAL_GPIO_ReadPin(GPIOx, GPIO_Pin);

    // 步骤3：转换为驱动层统一逻辑电平并返回。
    return (pin_state == GPIO_PIN_SET) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

gpio_level_e drv_gpio_read_pin(const drv_gpio_pin_t *pin)
{
    if (pin == NULL)
    {
        return GPIO_LEVEL_LOW;
    }

    return drv_gpio_read((GPIO_TypeDef *)pin->port, (uint16_t)pin->pin);
}

/**
 * @brief 翻转指定引脚输出电平。
 * @param GPIOx GPIO 端口句柄。
 * @param GPIO_Pin GPIO 引脚掩码。
 */
void drv_gpio_toggle(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    // 步骤1：端口句柄保护。
    if (GPIOx == NULL)
    {
        return;
    }

    // 步骤2：调用 HAL 翻转目标引脚输出。
    HAL_GPIO_TogglePin(GPIOx, GPIO_Pin);
}

void drv_gpio_toggle_pin(const drv_gpio_pin_t *pin)
{
    if (pin == NULL)
    {
        return;
    }

    drv_gpio_toggle((GPIO_TypeDef *)pin->port, (uint16_t)pin->pin);
}
