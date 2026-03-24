/**
 * @file    mod_sensor.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   循迹传感器模块实现（ctx 架构）。
 * @details
 * 1. 文件作用：实现传感器上下文生命周期、映射绑定、状态采样和权重计算。
 * 2. 解耦边界：仅处理采样与语义转换，不处理任务调度和控制策略。
 * 3. 下层依赖：通过 `drv_gpio` 完成输入电平采样。
 */

#include "mod_sensor.h"

#include <string.h>

static mod_sensor_ctx_t s_default_ctx; // s_default_ctx：模块默认上下文实例。

/**
 * @brief 判断上下文是否可用于采样。
 * @param ctx 目标上下文。
 * @return true 可用。
 * @return false 不可用。
 */
static bool mod_sensor_ctx_ready(const mod_sensor_ctx_t *ctx)
{
    return ((ctx != NULL) && ctx->inited && ctx->bound);
}

/**
 * @brief 校验单个映射项。
 * @param item 映射项指针。
 * @return true 合法。
 * @return false 非法。
 */
static bool mod_sensor_check_item(const mod_sensor_map_item_t *item)
{
    // 步骤1：端口句柄必须有效。
    if ((item == NULL) || (item->port == NULL))
    {
        return false;
    }

    // 步骤2：黑线有效电平必须是合法枚举值。
    if ((item->line_level != GPIO_LEVEL_LOW) && (item->line_level != GPIO_LEVEL_HIGH))
    {
        return false;
    }

    return true;
}

/**
 * @brief 执行一次全通道采样并更新缓存。
 * @param ctx 目标上下文。
 */
static void mod_sensor_sample(mod_sensor_ctx_t *ctx)
{
    uint8_t sampled_states[MOD_SENSOR_CHANNEL_NUM]; // sampled_states：本轮采样状态缓存。
    float sum_weight = 0.0f; // sum_weight：本轮权重累加值。

    // 步骤1：先清空本轮采样缓存。
    memset(sampled_states, 0, sizeof(sampled_states));

    // 步骤2：遍历全部通道，读取电平并计算权重。
    for (uint8_t i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++)
    {
        const mod_sensor_map_item_t *item = &ctx->map[i]; // item：当前通道映射配置。
        gpio_level_e level = drv_gpio_read(item->port, item->pin); // level：当前通道实时电平。

        if (level == item->line_level)
        {
            sampled_states[i] = 1U;
            sum_weight += item->factor;
        }
    }

    // 步骤3：权重限幅到 [-1, 1]，与上层控制接口约定保持一致。
    if (sum_weight > 1.0f)
    {
        sum_weight = 1.0f;
    }
    else if (sum_weight < -1.0f)
    {
        sum_weight = -1.0f;
    }

    // 步骤4：写回上下文缓存，供外部读取。
    memcpy(ctx->states, sampled_states, sizeof(ctx->states));
    ctx->weight = sum_weight;
}

mod_sensor_ctx_t *mod_sensor_get_default_ctx(void)
{
    return &s_default_ctx;
}

bool mod_sensor_ctx_init(mod_sensor_ctx_t *ctx, const mod_sensor_bind_t *bind)
{
    // 步骤1：参数校验。
    if (ctx == NULL)
    {
        return false;
    }

    // 步骤2：清空历史状态并标记初始化完成。
    memset(ctx, 0, sizeof(*ctx));
    ctx->inited = true;

    // 步骤3：支持“初始化即绑定”。
    if (bind != NULL)
    {
        return mod_sensor_bind(ctx, bind);
    }

    return true;
}

void mod_sensor_ctx_deinit(mod_sensor_ctx_t *ctx)
{
    // 步骤1：空指针保护。
    if (ctx == NULL)
    {
        return;
    }

    // 步骤2：先解绑再清零。
    mod_sensor_unbind(ctx);
    memset(ctx, 0, sizeof(*ctx));
}

bool mod_sensor_bind(mod_sensor_ctx_t *ctx, const mod_sensor_bind_t *bind)
{
    // 步骤1：参数与状态校验。
    if ((ctx == NULL) || (!ctx->inited) || (bind == NULL) || (bind->map == NULL) ||
        (bind->map_num != MOD_SENSOR_CHANNEL_NUM))
    {
        return false;
    }

    // 步骤2：逐项校验映射合法性。
    for (uint8_t i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++)
    {
        if (!mod_sensor_check_item(&bind->map[i]))
        {
            return false;
        }
    }

    // 步骤3：保存映射副本并标记绑定成功。
    memcpy(ctx->map, bind->map, sizeof(ctx->map));
    ctx->bound = true;

    // 步骤4：初始化运行缓存。
    mod_sensor_init(ctx);
    return true;
}

void mod_sensor_unbind(mod_sensor_ctx_t *ctx)
{
    // 步骤1：仅对已初始化上下文执行解绑。
    if ((ctx == NULL) || (!ctx->inited))
    {
        return;
    }

    // 步骤2：清空映射与缓存并复位绑定标志。
    memset(ctx->map, 0, sizeof(ctx->map));
    memset(ctx->states, 0, sizeof(ctx->states));
    ctx->weight = 0.0f;
    ctx->bound = false;
}

bool mod_sensor_is_bound(const mod_sensor_ctx_t *ctx)
{
    return mod_sensor_ctx_ready(ctx);
}

void mod_sensor_init(mod_sensor_ctx_t *ctx)
{
    // 步骤1：仅对已初始化上下文清缓存。
    if ((ctx == NULL) || (!ctx->inited))
    {
        return;
    }

    // 步骤2：复位输出缓存状态。
    memset(ctx->states, 0, sizeof(ctx->states));
    ctx->weight = 0.0f;
}

bool mod_sensor_get_states(mod_sensor_ctx_t *ctx, uint8_t *states, uint8_t states_num)
{
    // 步骤1：参数和就绪状态校验。
    if (!mod_sensor_ctx_ready(ctx) || (states == NULL) || (states_num < MOD_SENSOR_CHANNEL_NUM))
    {
        return false;
    }

    // 步骤2：执行一次采样并输出缓存状态。
    mod_sensor_sample(ctx);
    memcpy(states, ctx->states, MOD_SENSOR_CHANNEL_NUM);

    return true;
}

float mod_sensor_get_weight(mod_sensor_ctx_t *ctx)
{
    // 步骤1：就绪状态校验。
    if (!mod_sensor_ctx_ready(ctx))
    {
        return 0.0f;
    }

    // 步骤2：执行一次采样并返回权重结果。
    mod_sensor_sample(ctx);
    return ctx->weight;
}
