/**
 * @file    mod_vision.c
 * @author  Amber Ge
 * @brief   统一视觉语义模块实现。
 */

#include "mod_vision.h"

#include "stm32f4xx_hal.h"

#include <string.h>

static mod_vision_ctx_t s_default_ctx;

static uint32_t _critical_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void _critical_exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

mod_vision_ctx_t *mod_vision_get_default_ctx(void)
{
    return &s_default_ctx;
}

bool mod_vision_ctx_init(mod_vision_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }

    memset(ctx, 0, sizeof(mod_vision_ctx_t));
    ctx->inited = true;
    return true;
}

void mod_vision_ctx_deinit(mod_vision_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(mod_vision_ctx_t));
}

void mod_vision_clear(mod_vision_ctx_t *ctx)
{
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    primask = _critical_enter();
    memset(&ctx->latest, 0, sizeof(ctx->latest));
    _critical_exit(primask);
}

bool mod_vision_get_latest_data(const mod_vision_ctx_t *ctx, mod_vision_data_t *out_data)
{
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited || (out_data == NULL))
    {
        return false;
    }

    primask = _critical_enter();
    *out_data = ctx->latest;
    _critical_exit(primask);
    return out_data->valid;
}

bool mod_vision_has_valid_data(const mod_vision_ctx_t *ctx)
{
    bool valid;
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited)
    {
        return false;
    }

    primask = _critical_enter();
    valid = ctx->latest.valid;
    _critical_exit(primask);
    return valid;
}

bool mod_vision_is_data_stale(const mod_vision_ctx_t *ctx, uint32_t timeout_ms)
{
    uint32_t update_tick;
    bool valid;
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited)
    {
        return true;
    }

    primask = _critical_enter();
    valid = ctx->latest.valid;
    update_tick = ctx->latest.update_tick;
    _critical_exit(primask);

    if (!valid)
    {
        return true;
    }

    return ((HAL_GetTick() - update_tick) > timeout_ms);
}

bool mod_vision_update_from_k230(mod_vision_ctx_t *ctx,
                                 uint8_t x_target_id,
                                 int16_t x_error,
                                 uint8_t y_target_id,
                                 int16_t y_error)
{
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited)
    {
        return false;
    }

    primask = _critical_enter();
    ctx->latest.valid = true;
    ctx->latest.source = MOD_VISION_SOURCE_K230;
    ctx->latest.update_seq++;
    ctx->latest.update_tick = HAL_GetTick();
    ctx->latest.x_target_id = x_target_id;
    ctx->latest.x_error = x_error;
    ctx->latest.y_target_id = y_target_id;
    ctx->latest.y_error = y_error;
    _critical_exit(primask);

    return true;
}
