/**
 * @file    drv_gpio.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   GPIO 驱动统一接口声明。
 * @details
 * 1. 本文件仅提供 GPIO 原子能力：读、写、翻转。
 * 2. 本文件不承载业务语义，不区分 LED/继电器/按键等设备角色。
 * 3. 逻辑电平抽象由 `gpio_level_e` 给出，屏蔽 HAL `GPIO_PinState` 细节。
 * 4. 上层模块（`mod_led/mod_relay/mod_sensor/mod_key/mod_motor`）均通过本接口访问引脚。
 * 5. GPIO 时钟与模式配置由 Core 初始化代码负责，本驱动不重复配置硬件。
 */
#ifndef FINAL_GRADUATE_WORK_DRV_GPIO_H
#define FINAL_GRADUATE_WORK_DRV_GPIO_H

#include "main.h"

/**
 * @brief GPIO 逻辑电平抽象。
 */
typedef enum
{
    GPIO_LEVEL_LOW = 0,   // 逻辑低电平
    GPIO_LEVEL_HIGH = 1   // 逻辑高电平
} gpio_level_e;

/**
 * @brief 向指定引脚写入逻辑电平。
 * @param GPIOx GPIO 端口句柄，例如 `GPIOA`。
 * @param GPIO_Pin GPIO 引脚掩码，例如 `GPIO_PIN_5`。
 * @param level 目标逻辑电平。
 * @note 本接口不校验端口与引脚是否已完成初始化，调用方需保证硬件已就绪。
 */
void drv_gpio_write(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, gpio_level_e level);

/**
 * @brief 读取指定引脚的逻辑电平。
 * @param GPIOx GPIO 端口句柄。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @return gpio_level_e 当前逻辑电平。
 */
gpio_level_e drv_gpio_read(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

/**
 * @brief 翻转指定引脚输出电平。
 * @param GPIOx GPIO 端口句柄。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @note 翻转动作仅对配置为输出模式的引脚有意义。
 */
void drv_gpio_toggle(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

#endif /* FINAL_GRADUATE_WORK_DRV_GPIO_H */
