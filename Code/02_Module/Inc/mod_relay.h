/**
 ******************************************************************************
 * @file    mod_relay.h
 * @brief   继电器模块接口
 * @details
 * 1. 模块层负责继电器逻辑ID到GPIO资源的映射。
 * 2. 模块不再提供默认映射，必须先显式绑定。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_RELAY_H
#define FINAL_GRADUATE_WORK_MOD_RELAY_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"

typedef enum
{
    RELAY_LASER = 0, // 激光输出逻辑ID
    RELAY_BUZZER,    // 蜂鸣器输出逻辑ID
    RELAY_MAX
} mod_relay_id_e;

/** 单路继电器硬件绑定配置 */
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    gpio_level_e active_level;
} mod_relay_hw_cfg_t;

bool mod_relay_bind_map(const mod_relay_hw_cfg_t *map, uint8_t map_num);
void mod_relay_unbind_map(void);
bool mod_relay_is_bound(void);

void mod_relay_init(void);
void mod_relay_on(mod_relay_id_e relay);
void mod_relay_off(mod_relay_id_e relay);
void mod_relay_toggle(mod_relay_id_e relay);

#endif /* FINAL_GRADUATE_WORK_MOD_RELAY_H */
