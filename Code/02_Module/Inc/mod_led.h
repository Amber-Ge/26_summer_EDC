/**
 ******************************************************************************
 * @file    mod_led.h
 * @brief   LED模块接口
 * @details
 * 1. 模块层负责LED逻辑ID到GPIO资源的映射。
 * 2. 模块不再提供默认映射，必须先显式绑定。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_LED_H
#define FINAL_GRADUATE_WORK_MOD_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"

typedef enum
{
    LED_RED = 0,
    LED_GREEN,
    LED_YELLOW,
    LED_MAX
} mod_led_id_e;

/** 单路LED硬件绑定配置 */
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    gpio_level_e active_level;
} mod_led_hw_cfg_t;

bool mod_led_bind_map(const mod_led_hw_cfg_t *map, uint8_t map_num);
void mod_led_unbind_map(void);
bool mod_led_is_bound(void);

void mod_led_Init(void);
void mod_led_on(mod_led_id_e led);
void mod_led_off(mod_led_id_e led);
void mod_led_toggle(mod_led_id_e led);

#endif /* FINAL_GRADUATE_WORK_MOD_LED_H */