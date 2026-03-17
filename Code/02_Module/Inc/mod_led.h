/**
 ******************************************************************************
 * @file    mod_led.h
 * @brief   LED 模块接口定义
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_LED_H
#define FINAL_GRADUATE_WORK_MOD_LED_H // 头文件防重复包含宏

#include "drv_gpio.h"

/**
 * @brief LED 编号枚举。
 */
typedef enum
{
    LED_RED = 0, // 红色LED
    LED_GREEN, // 绿色LED
    LED_YELLOW, // 黄色LED
    LED_MAX // LED数量上限
} mod_led_id_e;

/**
 * @brief 初始化 LED 模块。
 * @details 上电后默认关闭全部 LED。
 */
void mod_led_Init(void);

/**
 * @brief 点亮指定 LED。
 * @param led LED 编号。
 */
void mod_led_on(mod_led_id_e led);

/**
 * @brief 熄灭指定 LED。
 * @param led LED 编号。
 */
void mod_led_off(mod_led_id_e led);

/**
 * @brief 翻转指定 LED 状态。
 * @param led LED 编号。
 */
void mod_led_toggle(mod_led_id_e led);

#endif
