/**
 * @file    drv_gpio.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   GPIO 驱动层统一接口定义。
 * @details
 * 1. 文件作用：封装 GPIO 读/写/翻转操作并统一逻辑电平抽象。
 * 2. 解耦边界：驱动层只负责引脚访问，不负责设备语义（LED/继电器/传感器等）。
 * 3. 上层绑定：`mod_led`、`mod_relay`、`mod_sensor`、`mod_key` 等模块复用该接口。
 * 4. 下层依赖：直接调用 HAL GPIO 接口与 `main.h` 中端口/引脚定义。
 * 5. 生命周期：GPIO 时钟和模式需由 Core 初始化代码预先配置完成。
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
 * @brief 写入指定 GPIO 引脚的逻辑电平。
 * @param GPIOx GPIO 端口（如 `GPIOA`）。
 * @param GPIO_Pin GPIO 引脚掩码（如 `GPIO_PIN_5`）。
 * @param level 目标逻辑电平（`GPIO_LEVEL_LOW` 或 `GPIO_LEVEL_HIGH`）。
 * @return 无返回值。
 */
void drv_gpio_write(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, gpio_level_e level);

/**
 * @brief 读取指定 GPIO 引脚的逻辑电平。
 * @param GPIOx GPIO 端口。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @return gpio_level_e 当前引脚逻辑电平。
 */
gpio_level_e drv_gpio_read(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

/**
 * @brief 翻转指定 GPIO 引脚电平。
 * @param GPIOx GPIO 端口。
 * @param GPIO_Pin GPIO 引脚掩码。
 * @return 无返回值。
 */
void drv_gpio_toggle(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

#endif /* FINAL_GRADUATE_WORK_DRV_GPIO_H */
