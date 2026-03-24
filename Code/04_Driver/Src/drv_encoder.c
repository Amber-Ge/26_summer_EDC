/**
 * @file    drv_encoder.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   正交编码器驱动层接口实现。
 * @details
 * 1. 文件作用：实现编码器 ctx 初始化、启停、清零与增量读取。
 * 2. 解耦边界：仅处理计数采集和方向映射，不承担速度闭环逻辑。
 * 3. 上层绑定：`mod_motor` 周期调用 `drv_encoder_get_delta` 更新反馈。
 * 4. 下层依赖：HAL TIM 编码器模式接口。
 */

#include "drv_encoder.h"

/**
 * @brief 校验计数器位宽参数是否合法。
 * @param counter_bits 计数器位宽配置值。
 * @return true 参数合法。
 * @return false 参数非法。
 */
static bool drv_encoder_is_valid_counter_bits(uint8_t counter_bits)
{
    return (bool)((counter_bits == DRV_ENCODER_BITS_16) ||
                  (counter_bits == DRV_ENCODER_BITS_32));
}

/**
 * @brief 判断编码器 ctx 是否完成初始化。
 * @param ctx 编码器上下文。
 * @return true 已初始化。
 * @return false 未初始化或字段非法。
 */
static bool drv_encoder_is_ctx_inited(const drv_encoder_ctx_t *ctx)
{
    // 步骤1：对象和硬件句柄必须有效。
    if ((ctx == NULL) || (ctx->htim == NULL))
    {
        return false;
    }

    // 步骤2：位宽和方向系数必须在支持范围内。
    if ((!drv_encoder_is_valid_counter_bits(ctx->counter_bits)) ||
        ((ctx->direction != 1) && (ctx->direction != -1)))
    {
        return false;
    }

    // 步骤3：通过全部静态配置检查。
    return true;
}

/**
 * @brief 初始化编码器上下文。
 * @param ctx 编码器上下文指针。
 * @param htim 编码器模式定时器句柄。
 * @param counter_bits 计数器位宽（16/32）。
 * @param invert 方向反转标志。
 * @return drv_encoder_status_e 状态码。
 */
drv_encoder_status_e drv_encoder_ctx_init(drv_encoder_ctx_t *ctx,
                                          TIM_HandleTypeDef *htim,
                                          uint8_t counter_bits,
                                          bool invert)
{
    // 步骤1：参数校验。
    if ((ctx == NULL) || (htim == NULL) || (!drv_encoder_is_valid_counter_bits(counter_bits)))
    {
        return DRV_ENCODER_STATUS_INVALID_PARAM;
    }

    // 步骤2：写入静态配置并清空运行态。
    ctx->htim = htim;
    ctx->counter_bits = counter_bits;
    ctx->direction = invert ? -1 : 1;
    ctx->delta_last = 0;
    ctx->started = false;

    // 步骤3：建立统一基线，初始化后计数器归零。
    __HAL_TIM_SET_COUNTER(ctx->htim, 0U);

    // 步骤4：返回成功。
    return DRV_ENCODER_STATUS_OK;
}

/**
 * @brief 启动编码器计数。
 * @param ctx 编码器上下文指针。
 * @return drv_encoder_status_e 状态码。
 */
drv_encoder_status_e drv_encoder_start(drv_encoder_ctx_t *ctx)
{
    // 步骤1：空指针保护。
    if (ctx == NULL)
    {
        return DRV_ENCODER_STATUS_INVALID_PARAM;
    }

    // 步骤2：初始化状态校验。
    if (!drv_encoder_is_ctx_inited(ctx))
    {
        return DRV_ENCODER_STATUS_INVALID_STATE;
    }

    // 步骤3：幂等处理，已启动直接成功返回。
    if (ctx->started)
    {
        return DRV_ENCODER_STATUS_OK;
    }

    // 步骤4：调用 HAL 启动编码器模式。
    if (HAL_TIM_Encoder_Start(ctx->htim, TIM_CHANNEL_ALL) != HAL_OK)
    {
        ctx->started = false;
        return DRV_ENCODER_STATUS_HAL_FAIL;
    }

    // 步骤5：启动后清零计数器与缓存，保证统计窗口一致。
    __HAL_TIM_SET_COUNTER(ctx->htim, 0U);
    ctx->delta_last = 0;
    ctx->started = true;

    return DRV_ENCODER_STATUS_OK;
}

/**
 * @brief 停止编码器计数。
 * @param ctx 编码器上下文指针。
 * @return drv_encoder_status_e 状态码。
 */
drv_encoder_status_e drv_encoder_stop(drv_encoder_ctx_t *ctx)
{
    // 步骤1：空指针保护。
    if (ctx == NULL)
    {
        return DRV_ENCODER_STATUS_INVALID_PARAM;
    }

    // 步骤2：初始化状态校验。
    if (!drv_encoder_is_ctx_inited(ctx))
    {
        return DRV_ENCODER_STATUS_INVALID_STATE;
    }

    // 步骤3：幂等处理，未启动直接成功返回。
    if (!ctx->started)
    {
        return DRV_ENCODER_STATUS_OK;
    }

    // 步骤4：调用 HAL 停止编码器模式。
    if (HAL_TIM_Encoder_Stop(ctx->htim, TIM_CHANNEL_ALL) != HAL_OK)
    {
        return DRV_ENCODER_STATUS_HAL_FAIL;
    }

    // 步骤5：停止后复位运行态并清零计数器。
    ctx->started = false;
    ctx->delta_last = 0;
    __HAL_TIM_SET_COUNTER(ctx->htim, 0U);

    return DRV_ENCODER_STATUS_OK;
}

/**
 * @brief 清零编码器计数器。
 * @param ctx 编码器上下文指针。
 * @return drv_encoder_status_e 状态码。
 */
drv_encoder_status_e drv_encoder_reset(drv_encoder_ctx_t *ctx)
{
    // 步骤1：空指针保护。
    if (ctx == NULL)
    {
        return DRV_ENCODER_STATUS_INVALID_PARAM;
    }

    // 步骤2：初始化状态校验。
    if (!drv_encoder_is_ctx_inited(ctx))
    {
        return DRV_ENCODER_STATUS_INVALID_STATE;
    }

    // 步骤3：清零硬件计数与软件缓存。
    __HAL_TIM_SET_COUNTER(ctx->htim, 0U);
    ctx->delta_last = 0;

    return DRV_ENCODER_STATUS_OK;
}

/**
 * @brief 读取自上次调用以来的编码器增量。
 * @param ctx 编码器上下文指针。
 * @param delta_out 输出增量指针。
 * @return drv_encoder_status_e 状态码。
 */
drv_encoder_status_e drv_encoder_get_delta(drv_encoder_ctx_t *ctx, int32_t *delta_out)
{
    int32_t delta_raw = 0; // delta_raw：按位宽解释后的原始计数增量。
    int32_t delta_signed = 0; // delta_signed：应用方向因子后的最终增量。

    // 步骤1：参数校验。
    if ((ctx == NULL) || (delta_out == NULL))
    {
        return DRV_ENCODER_STATUS_INVALID_PARAM;
    }

    // 步骤2：要求 ctx 已初始化且处于启动状态。
    if ((!drv_encoder_is_ctx_inited(ctx)) || (!ctx->started))
    {
        return DRV_ENCODER_STATUS_INVALID_STATE;
    }

    // 步骤3：按配置位宽读取当前计数值。
    if (ctx->counter_bits == DRV_ENCODER_BITS_16)
    {
        delta_raw = (int32_t)((int16_t)__HAL_TIM_GET_COUNTER(ctx->htim));
    }
    else
    {
        delta_raw = (int32_t)__HAL_TIM_GET_COUNTER(ctx->htim);
    }

    // 步骤4：读取后立即清零计数器，形成“读后清零”的窗口模型。
    __HAL_TIM_SET_COUNTER(ctx->htim, 0U);

    // 步骤5：应用方向因子并输出。
    delta_signed = delta_raw * (int32_t)ctx->direction;
    *delta_out = delta_signed;
    ctx->delta_last = delta_signed;

    return DRV_ENCODER_STATUS_OK;
}

/**
 * @brief 查询编码器是否已启动。
 * @param ctx 编码器上下文指针。
 * @return true 已启动。
 * @return false 未启动或参数非法。
 */
bool drv_encoder_is_started(const drv_encoder_ctx_t *ctx)
{
    return (bool)(drv_encoder_is_ctx_inited(ctx) && ctx->started);
}
