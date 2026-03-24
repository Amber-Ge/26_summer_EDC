/**
 * @file    mod_led.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   LED 模块接口（ctx 架构）。
 * @details
 * 1. 模块职责：建立 LED 逻辑通道与 GPIO 引脚的映射，并提供统一开关控制接口。
 * 2. 解耦边界：本模块只处理 LED 语义，不暴露 HAL 或引脚电平细节给任务层。
 * 3. 生命周期：`ctx_init -> bind -> init -> on/off/toggle -> unbind/deinit`。
 * 4. 兼容策略：仅保留 ctx 架构接口，不提供旧版全局兼容接口。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_LED_H
#define FINAL_GRADUATE_WORK_MOD_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"

/**
 * @brief LED 逻辑通道 ID。
 */
typedef enum
{
    LED_RED = 0, // 红灯
    LED_GREEN,   // 绿灯
    LED_YELLOW,  // 黄灯
    LED_MAX      // 通道数量上限
} mod_led_id_e;

/**
 * @brief 单路 LED 的硬件映射配置。
 */
typedef struct
{
    GPIO_TypeDef *port;        // GPIO 端口
    uint16_t pin;              // GPIO 引脚掩码
    gpio_level_e active_level; // 点亮有效电平
} mod_led_hw_cfg_t;

/**
 * @brief LED 模块绑定参数。
 */
typedef struct
{
    const mod_led_hw_cfg_t *map; // 映射表首地址
    uint8_t map_num;             // 映射表数量，必须等于 `LED_MAX`
} mod_led_bind_t;

/**
 * @brief LED 模块运行上下文。
 */
typedef struct
{
    bool inited;                   // 上下文是否已初始化
    bool bound;                    // 是否已绑定映射
    mod_led_hw_cfg_t map[LED_MAX]; // 生效映射副本
} mod_led_ctx_t;

/**
 * @brief 获取模块默认上下文。
 * @return mod_led_ctx_t* 默认上下文地址。
 */
mod_led_ctx_t *mod_led_get_default_ctx(void);

/**
 * @brief 初始化 LED 上下文，可选在初始化阶段直接绑定映射。
 * @param ctx 目标上下文，不可为 NULL。
 * @param bind 可选绑定参数，为 NULL 表示仅初始化。
 * @return true 初始化成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_led_ctx_init(mod_led_ctx_t *ctx, const mod_led_bind_t *bind);

/**
 * @brief 反初始化 LED 上下文。
 * @param ctx 目标上下文。
 */
void mod_led_ctx_deinit(mod_led_ctx_t *ctx);

/**
 * @brief 绑定 LED 映射到指定上下文。
 * @param ctx 目标上下文，必须已初始化。
 * @param bind 绑定参数，不可为 NULL。
 * @return true 绑定成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_led_bind(mod_led_ctx_t *ctx, const mod_led_bind_t *bind);

/**
 * @brief 解绑 LED 映射。
 * @param ctx 目标上下文。
 */
void mod_led_unbind(mod_led_ctx_t *ctx);

/**
 * @brief 查询上下文是否已可用于运行期控制。
 * @param ctx 目标上下文。
 * @return true 上下文已就绪。
 * @return false 上下文未就绪。
 */
bool mod_led_is_bound(const mod_led_ctx_t *ctx);

/**
 * @brief 执行运行期初始化（默认全部熄灭）。
 * @param ctx 目标上下文。
 */
void mod_led_init(mod_led_ctx_t *ctx);

/**
 * @brief 点亮指定 LED。
 * @param ctx 目标上下文。
 * @param led LED 通道 ID。
 */
void mod_led_on(mod_led_ctx_t *ctx, mod_led_id_e led);

/**
 * @brief 熄灭指定 LED。
 * @param ctx 目标上下文。
 * @param led LED 通道 ID。
 */
void mod_led_off(mod_led_ctx_t *ctx, mod_led_id_e led);

/**
 * @brief 翻转指定 LED 输出。
 * @param ctx 目标上下文。
 * @param led LED 通道 ID。
 */
void mod_led_toggle(mod_led_ctx_t *ctx, mod_led_id_e led);

#endif /* FINAL_GRADUATE_WORK_MOD_LED_H */
