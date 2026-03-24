/**
 * @file    mod_battery.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   电池电压模块实现（ctx 架构）。
 * @details
 * 1. 文件作用：实现电池参数绑定、生命周期管理与电压缓存更新。
 * 2. 解耦边界：模块层负责电压语义转换；驱动层只提供 ADC 原始采样。
 * 3. 上层调用：任务层按周期调用 `update`，显示或控制逻辑调用 `get_voltage`。
 */

#include "mod_battery.h"

#include <string.h>

#include "drv_adc.h"

static mod_battery_ctx_t s_default_ctx; // s_default_ctx：模块默认上下文实例。

/**
 * @brief 判断上下文是否已就绪。
 * @param ctx 上下文指针。
 * @return true 已初始化并绑定。
 * @return false 未就绪。
 */
static bool mod_battery_ctx_ready(const mod_battery_ctx_t *ctx)
{
    return ((ctx != NULL) && ctx->inited && ctx->bound && (ctx->hadc != NULL));
}

/**
 * @brief 校验绑定参数是否合法。
 * @param bind 绑定参数指针。
 * @return true 合法。
 * @return false 非法。
 */
static bool mod_battery_check_bind(const mod_battery_bind_t *bind)
{
    // 步骤1：基础指针与 ADC 句柄必须有效。
    if ((bind == NULL) || (bind->hadc == NULL))
    {
        return false;
    }

    // 步骤2：换算参数必须大于 0，避免除零或负值语义错误。
    if ((bind->adc_ref_v <= 0.0f) || (bind->adc_res <= 0.0f) || (bind->voltage_ratio <= 0.0f))
    {
        return false;
    }

    // 步骤3：采样次数必须至少为 1。
    if (bind->sample_cnt == 0U)
    {
        return false;
    }

    return true;
}

mod_battery_ctx_t *mod_battery_get_default_ctx(void)
{
    return &s_default_ctx;
}

bool mod_battery_ctx_init(mod_battery_ctx_t *ctx, const mod_battery_bind_t *bind)
{
    // 步骤1：参数校验。
    if (ctx == NULL)
    {
        return false;
    }

    // 步骤2：清理旧状态并标记已初始化。
    memset(ctx, 0, sizeof(*ctx));
    ctx->inited = true;

    // 步骤3：支持“初始化即绑定”便捷路径。
    if (bind != NULL)
    {
        return mod_battery_bind(ctx, bind);
    }

    return true;
}

void mod_battery_ctx_deinit(mod_battery_ctx_t *ctx)
{
    // 步骤1：空指针保护。
    if (ctx == NULL)
    {
        return;
    }

    // 步骤2：先解绑再清空，保证生命周期回收完整。
    mod_battery_unbind(ctx);
    memset(ctx, 0, sizeof(*ctx));
}

bool mod_battery_bind(mod_battery_ctx_t *ctx, const mod_battery_bind_t *bind)
{
    // 步骤1：上下文状态与绑定参数联合校验。
    if ((ctx == NULL) || (!ctx->inited) || (!mod_battery_check_bind(bind)))
    {
        return false;
    }

    // 步骤2：写入生效配置。
    ctx->hadc = bind->hadc;
    ctx->adc_ref_v = bind->adc_ref_v;
    ctx->adc_res = bind->adc_res;
    ctx->voltage_ratio = bind->voltage_ratio;
    ctx->sample_cnt = bind->sample_cnt;

    // 步骤3：标记绑定完成。
    ctx->bound = true;
    return true;
}

void mod_battery_unbind(mod_battery_ctx_t *ctx)
{
    // 步骤1：仅在已初始化上下文上执行解绑。
    if ((ctx == NULL) || (!ctx->inited))
    {
        return;
    }

    // 步骤2：清空配置字段与绑定标志。
    ctx->hadc = NULL;
    ctx->adc_ref_v = 0.0f;
    ctx->adc_res = 0.0f;
    ctx->voltage_ratio = 0.0f;
    ctx->sample_cnt = 0U;
    ctx->bound = false;
}

bool mod_battery_is_bound(const mod_battery_ctx_t *ctx)
{
    return mod_battery_ctx_ready(ctx);
}

bool mod_battery_update(mod_battery_ctx_t *ctx)
{
    uint16_t raw_avg = 0U; // raw_avg：平均 ADC 原始值。
    float voltage_on_pin = 0.0f; // voltage_on_pin：分压前引脚电压值。

    // 步骤1：运行前置状态校验。
    if (!mod_battery_ctx_ready(ctx))
    {
        return false;
    }

    // 步骤2：读取多次平均原始值。
    if (!drv_adc_read_raw_avg(ctx->hadc, ctx->sample_cnt, &raw_avg))
    {
        return false;
    }

    // 步骤3：执行电压换算并更新缓存。
    voltage_on_pin = ((float)raw_avg / ctx->adc_res) * ctx->adc_ref_v;
    ctx->cached_voltage = voltage_on_pin * ctx->voltage_ratio;

    return true;
}

float mod_battery_get_voltage(const mod_battery_ctx_t *ctx)
{
    // 步骤1：空指针保护。
    if (ctx == NULL)
    {
        return 0.0f;
    }

    // 步骤2：返回缓存电压。
    return ctx->cached_voltage;
}
