/**
 * @file    mod_relay.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   继电器模块实现（ctx 架构）。
 * @details
 * 1. 文件作用：实现继电器映射绑定、生命周期管理与通道控制。
 * 2. 解耦边界：模块层对任务层暴露“吸合/断开”语义，不暴露引脚细节。
 * 3. 下层依赖：通过 `drv_gpio` 执行原子引脚操作。
 */

#include "mod_relay.h"

#include <string.h>

static mod_relay_ctx_t s_default_ctx; // s_default_ctx：模块默认上下文实例。

/**
 * @brief 校验继电器通道 ID。
 * @param relay 继电器通道枚举值。
 * @return true 合法。
 * @return false 非法。
 */
static bool mod_relay_is_valid_id(mod_relay_id_e relay)
{
    return ((relay >= RELAY_LASER) && (relay < RELAY_MAX));
}

/**
 * @brief 逻辑电平取反工具。
 * @param level 输入电平。
 * @return gpio_level_e 反向后的电平。
 */
static gpio_level_e mod_relay_invert_level(gpio_level_e level)
{
    return (level == GPIO_LEVEL_LOW) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

/**
 * @brief 判断上下文是否可运行。
 * @param ctx 上下文指针。
 * @return true 已初始化并绑定。
 * @return false 未就绪。
 */
static bool mod_relay_ctx_ready(const mod_relay_ctx_t *ctx)
{
    return ((ctx != NULL) && ctx->inited && ctx->bound);
}

/**
 * @brief 校验单个继电器映射项。
 * @param item 映射项指针。
 * @return true 合法。
 * @return false 非法。
 */
static bool mod_relay_check_item(const mod_relay_hw_cfg_t *item)
{
    // 步骤1：端口句柄必须有效。
    if ((item == NULL) || (item->port == NULL))
    {
        return false;
    }

    // 步骤2：有效电平必须在枚举定义范围内。
    if ((item->active_level != GPIO_LEVEL_LOW) && (item->active_level != GPIO_LEVEL_HIGH))
    {
        return false;
    }

    return true;
}

mod_relay_ctx_t *mod_relay_get_default_ctx(void)
{
    return &s_default_ctx;
}

bool mod_relay_ctx_init(mod_relay_ctx_t *ctx, const mod_relay_bind_t *bind)
{
    // 步骤1：参数校验。
    if (ctx == NULL)
    {
        return false;
    }

    // 步骤2：清空历史状态并设置初始化标志。
    memset(ctx, 0, sizeof(*ctx));
    ctx->inited = true;

    // 步骤3：支持“初始化即绑定”。
    if (bind != NULL)
    {
        return mod_relay_bind(ctx, bind);
    }

    return true;
}

void mod_relay_ctx_deinit(mod_relay_ctx_t *ctx)
{
    // 步骤1：空指针保护。
    if (ctx == NULL)
    {
        return;
    }

    // 步骤2：先解绑再清零。
    mod_relay_unbind(ctx);
    memset(ctx, 0, sizeof(*ctx));
}

bool mod_relay_bind(mod_relay_ctx_t *ctx, const mod_relay_bind_t *bind)
{
    // 步骤1：参数与状态校验。
    if ((ctx == NULL) || (!ctx->inited) || (bind == NULL) || (bind->map == NULL) || (bind->map_num != RELAY_MAX))
    {
        return false;
    }

    // 步骤2：逐项校验映射合法性。
    for (uint8_t i = 0U; i < RELAY_MAX; i++)
    {
        if (!mod_relay_check_item(&bind->map[i]))
        {
            return false;
        }
    }

    // 步骤3：保存映射副本并标记绑定完成。
    memcpy(ctx->map, bind->map, sizeof(ctx->map));
    ctx->bound = true;

    return true;
}

void mod_relay_unbind(mod_relay_ctx_t *ctx)
{
    // 步骤1：仅对已初始化上下文执行解绑。
    if ((ctx == NULL) || (!ctx->inited))
    {
        return;
    }

    // 步骤2：清空映射并复位绑定标志。
    memset(ctx->map, 0, sizeof(ctx->map));
    ctx->bound = false;
}

bool mod_relay_is_bound(const mod_relay_ctx_t *ctx)
{
    return mod_relay_ctx_ready(ctx);
}

void mod_relay_init(mod_relay_ctx_t *ctx)
{
    // 步骤1：就绪检查。
    if (!mod_relay_ctx_ready(ctx))
    {
        return;
    }

    // 步骤2：上电统一断开全部继电器，建立安全初始态。
    for (uint8_t i = 0U; i < RELAY_MAX; i++)
    {
        mod_relay_off(ctx, (mod_relay_id_e)i);
    }
}

void mod_relay_on(mod_relay_ctx_t *ctx, mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg = NULL; // cfg：当前通道硬件映射。

    // 步骤1：上下文和通道校验。
    if (!mod_relay_ctx_ready(ctx) || (!mod_relay_is_valid_id(relay)))
    {
        return;
    }

    // 步骤2：按 active_level 输出吸合命令。
    cfg = &ctx->map[(uint8_t)relay];
    drv_gpio_write(cfg->port, cfg->pin, cfg->active_level);
}

void mod_relay_off(mod_relay_ctx_t *ctx, mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg = NULL; // cfg：当前通道硬件映射。

    // 步骤1：上下文和通道校验。
    if (!mod_relay_ctx_ready(ctx) || (!mod_relay_is_valid_id(relay)))
    {
        return;
    }

    // 步骤2：写入 active_level 反相电平实现断开。
    cfg = &ctx->map[(uint8_t)relay];
    drv_gpio_write(cfg->port, cfg->pin, mod_relay_invert_level(cfg->active_level));
}

void mod_relay_toggle(mod_relay_ctx_t *ctx, mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg = NULL; // cfg：当前通道硬件映射。

    // 步骤1：上下文和通道校验。
    if (!mod_relay_ctx_ready(ctx) || (!mod_relay_is_valid_id(relay)))
    {
        return;
    }

    // 步骤2：直接翻转引脚输出状态。
    cfg = &ctx->map[(uint8_t)relay];
    drv_gpio_toggle(cfg->port, cfg->pin);
}
