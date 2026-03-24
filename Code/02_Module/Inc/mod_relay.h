/**
 * @file    mod_relay.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   继电器模块接口。
 * @details
 * 1. 文件作用：维护继电器逻辑 ID 到 GPIO 映射，提供统一开关控制接口。
 * 2. 解耦边界：本模块只负责继电器执行层抽象，不承载告警节奏或业务状态机。
 * 3. 上层绑定：由 `GpioTask` 等任务按业务状态调用开关接口。
 * 4. 下层依赖：`drv_gpio` 执行电平写入，硬件映射通过 bind 接口注入。
 * 5. 生命周期：先 `bind_map` 再 `init`，运行期可通过 `is_bound` 校验配置有效性。
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
    GPIO_TypeDef *port;      // 继电器端口
    uint16_t pin;            // 继电器引脚
    gpio_level_e active_level; // 吸合有效电平
} mod_relay_hw_cfg_t;

bool mod_relay_bind_map(const mod_relay_hw_cfg_t *map, uint8_t map_num);
void mod_relay_unbind_map(void);
bool mod_relay_is_bound(void);

void mod_relay_init(void);
void mod_relay_on(mod_relay_id_e relay);
void mod_relay_off(mod_relay_id_e relay);
void mod_relay_toggle(mod_relay_id_e relay);

#endif /* FINAL_GRADUATE_WORK_MOD_RELAY_H */



