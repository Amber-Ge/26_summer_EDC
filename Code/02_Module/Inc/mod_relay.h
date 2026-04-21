/**
 * @file    mod_relay.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   继电器模块接口（ctx 架构）。
 * @details
 * 1. 模块职责：建立继电器逻辑通道与 GPIO 引脚映射，并提供统一吸合/断开控制接口。
 * 2. 解耦边界：任务层不直接操作端口引脚，统一通过模块语义接口调用。
 * 3. 生命周期：`ctx_init -> bind -> init -> on/off/toggle -> unbind/deinit`。
 * 4. 兼容策略：仅保留 ctx 架构接口，不提供旧版全局兼容接口。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_RELAY_H
#define FINAL_GRADUATE_WORK_MOD_RELAY_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"

/**
 * @brief 继电器逻辑通道 ID。
 */
typedef enum
{
    RELAY_LASER = 0, // 激光继电器
    RELAY_BUZZER,    // 蜂鸣器继电器
    RELAY_MAX        // 通道数量上限
} mod_relay_id_e;

/**
 * @brief 单路继电器的硬件映射配置。
 */
typedef struct
{
    drv_gpio_pin_t pin;        // GPIO 绑定
    gpio_level_e active_level; // 吸合有效电平
} mod_relay_hw_cfg_t;

/**
 * @brief 继电器模块绑定参数。
 */
typedef struct
{
    const mod_relay_hw_cfg_t *map; // 映射表首地址
    uint8_t map_num;               // 映射表数量，必须等于 `RELAY_MAX`
} mod_relay_bind_t;

/**
 * @brief 继电器模块运行上下文。
 */
typedef struct
{
    bool inited;                        // 上下文是否已初始化
    bool bound;                         // 是否已绑定映射
    mod_relay_hw_cfg_t map[RELAY_MAX];  // 生效映射副本
} mod_relay_ctx_t;

/**
 * @brief 获取模块默认上下文。
 * @return mod_relay_ctx_t* 默认上下文地址。
 */
mod_relay_ctx_t *mod_relay_get_default_ctx(void);

/**
 * @brief 初始化继电器上下文，可选在初始化阶段直接绑定映射。
 * @param ctx 目标上下文，不可为 NULL。
 * @param bind 可选绑定参数，为 NULL 表示仅初始化。
 * @return true 初始化成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_relay_ctx_init(mod_relay_ctx_t *ctx, const mod_relay_bind_t *bind);

/**
 * @brief 反初始化继电器上下文。
 * @param ctx 目标上下文。
 */
void mod_relay_ctx_deinit(mod_relay_ctx_t *ctx);

/**
 * @brief 绑定继电器映射到指定上下文。
 * @param ctx 目标上下文，必须已初始化。
 * @param bind 绑定参数，不可为 NULL。
 * @return true 绑定成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_relay_bind(mod_relay_ctx_t *ctx, const mod_relay_bind_t *bind);

/**
 * @brief 解绑继电器映射。
 * @param ctx 目标上下文。
 */
void mod_relay_unbind(mod_relay_ctx_t *ctx);

/**
 * @brief 查询上下文是否已可用于运行期控制。
 * @param ctx 目标上下文。
 * @return true 上下文已就绪。
 * @return false 上下文未就绪。
 */
bool mod_relay_is_bound(const mod_relay_ctx_t *ctx);

/**
 * @brief 执行运行期初始化（默认全部断开）。
 * @param ctx 目标上下文。
 */
void mod_relay_init(mod_relay_ctx_t *ctx);

/**
 * @brief 使指定继电器吸合。
 * @param ctx 目标上下文。
 * @param relay 继电器通道 ID。
 */
void mod_relay_on(mod_relay_ctx_t *ctx, mod_relay_id_e relay);

/**
 * @brief 使指定继电器断开。
 * @param ctx 目标上下文。
 * @param relay 继电器通道 ID。
 */
void mod_relay_off(mod_relay_ctx_t *ctx, mod_relay_id_e relay);

/**
 * @brief 翻转指定继电器输出状态。
 * @param ctx 目标上下文。
 * @param relay 继电器通道 ID。
 */
void mod_relay_toggle(mod_relay_ctx_t *ctx, mod_relay_id_e relay);

#endif /* FINAL_GRADUATE_WORK_MOD_RELAY_H */
