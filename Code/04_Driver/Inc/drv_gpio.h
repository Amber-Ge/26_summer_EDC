/**
 ******************************************************************************
 * @file    drv_gpio.h
 * @brief   GPIO 驱动层接口定义
 * @details
 * 提供通用 GPIO 读/写/翻转接口，统一上层对引脚电平的访问方式。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_DRV_GPIO_H
#define FINAL_GRADUATE_WORK_DRV_GPIO_H

#include "main.h"

/**
 * @brief GPIO 逻辑电平枚举。
 */
typedef enum
{
    GPIO_LEVEL_LOW = 0,   // 低电平
    GPIO_LEVEL_HIGH = 1   // 高电平
} gpio_level_e;

/**
 * @brief 写入指定 GPIO 引脚电平。
 * @param GPIOx GPIO 端口（如 `GPIOA`）。
 * @param GPIO_Pin GPIO 引脚掩码（如 `GPIO_PIN_5`）。
 * @param level 目标电平（`GPIO_LEVEL_LOW` 或 `GPIO_LEVEL_HIGH`）。
 */
void drv_gpio_write(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, gpio_level_e level);

/**
 * @brief 读取指定 GPIO 引脚电平。
 * @param GPIOx GPIO 端口。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @return gpio_level_e 当前引脚逻辑电平。
 */
gpio_level_e drv_gpio_read(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

/**
 * @brief 翻转指定 GPIO 引脚电平。
 * @param GPIOx GPIO 端口。
 * @param GPIO_Pin GPIO 引脚掩码。
 */
void drv_gpio_toggle(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

#endif /* FINAL_GRADUATE_WORK_DRV_GPIO_H */
