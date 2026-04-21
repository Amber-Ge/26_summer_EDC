/**
 * @file    drv_pwm.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   PWM 驱动层接口实现（ctx 架构，无兼容接口）。
 * @details
 * 1. 该文件实现 PWM ctx 的参数校验、启停控制和占空比写入。
 * 2. 所有对外函数均遵循“先校验参数，再校验状态，再执行硬件访问”的统一流程。
 * 3. 启停接口为幂等语义，便于上层在状态切换过程中重复调用且不引入副作用。
 */

#include "drv_pwm.h"

/**
 * @brief 判断 TIM 通道号是否合法。
 * @param channel 待校验通道。
 * @return true 通道受支持。
 * @return false 通道不受支持。
 */
static bool drv_pwm_is_valid_channel(uint32_t channel)
{
    if ((channel == TIM_CHANNEL_1) ||
        (channel == TIM_CHANNEL_2) ||
        (channel == TIM_CHANNEL_3) ||
        (channel == TIM_CHANNEL_4))
    {
        return true;
    }

    return false;
}

/**
 * @brief 判断 ctx 是否已完成静态初始化。
 * @param ctx 待校验上下文。
 * @return true 已初始化。
 * @return false 未初始化或字段非法。
 */
static bool drv_pwm_is_ctx_inited(const drv_pwm_ctx_t *ctx)
{
    /* 句柄必须存在，最大占空比必须有效，通道必须在支持范围内。 */
    if ((ctx == NULL) || (ctx->htim == NULL) || (ctx->duty_max == 0U))
    {
        return false;
    }

    if (!drv_pwm_is_valid_channel(ctx->channel))
    {
        return false;
    }

    return true;
}

/**
 * @brief 初始化 PWM ctx。
 * @param ctx 目标上下文对象。
 * @param htim TIM 句柄。
 * @param channel TIM 通道号。
 * @param duty_max 占空比上限。
 * @param invert 占空比反相开关。
 * @return drv_pwm_status_e 状态码。
 */
drv_pwm_status_e drv_pwm_ctx_init(drv_pwm_ctx_t *ctx,
                                  const drv_pwm_bind_t *bind)
{
    /* 步骤1：参数校验。 */
    if ((ctx == NULL) || (bind == NULL) || (bind->instance == NULL) ||
        (bind->duty_max == 0U) || (!drv_pwm_is_valid_channel(bind->channel)))
    {
        return DRV_PWM_STATUS_INVALID_PARAM;
    }

    /* 步骤2：写入静态配置字段。 */
    ctx->htim = (TIM_HandleTypeDef *)bind->instance;
    ctx->channel = bind->channel;
    ctx->duty_max = bind->duty_max;
    ctx->duty_last = 0U;
    ctx->invert = bind->invert;
    ctx->started = false;

    /* 步骤3：初始化时清零比较寄存器，建立安全基线。 */
    __HAL_TIM_SET_COMPARE(ctx->htim, ctx->channel, 0U);

    return DRV_PWM_STATUS_OK;
}

/**
 * @brief 启动 PWM 输出。
 * @param ctx PWM 上下文对象。
 * @return drv_pwm_status_e 状态码。
 */
drv_pwm_status_e drv_pwm_start(drv_pwm_ctx_t *ctx)
{
    /* 步骤1：空指针校验。 */
    if (ctx == NULL)
    {
        return DRV_PWM_STATUS_INVALID_PARAM;
    }

    /* 步骤2：初始化状态校验。 */
    if (!drv_pwm_is_ctx_inited(ctx))
    {
        return DRV_PWM_STATUS_INVALID_STATE;
    }

    /* 步骤3：幂等处理，已启动直接成功返回。 */
    if (ctx->started)
    {
        return DRV_PWM_STATUS_OK;
    }

    /* 步骤4：启动前先清零占空比，避免沿用旧输出。 */
    __HAL_TIM_SET_COMPARE(ctx->htim, ctx->channel, 0U);
    ctx->duty_last = 0U;

    /* 步骤5：调用 HAL 启动通道。 */
    if (HAL_TIM_PWM_Start(ctx->htim, ctx->channel) != HAL_OK)
    {
        ctx->started = false;
        return DRV_PWM_STATUS_HAL_FAIL;
    }

    ctx->started = true;
    return DRV_PWM_STATUS_OK;
}

/**
 * @brief 停止 PWM 输出。
 * @param ctx PWM 上下文对象。
 * @return drv_pwm_status_e 状态码。
 */
drv_pwm_status_e drv_pwm_stop(drv_pwm_ctx_t *ctx)
{
    /* 步骤1：空指针校验。 */
    if (ctx == NULL)
    {
        return DRV_PWM_STATUS_INVALID_PARAM;
    }

    /* 步骤2：初始化状态校验。 */
    if (!drv_pwm_is_ctx_inited(ctx))
    {
        return DRV_PWM_STATUS_INVALID_STATE;
    }

    /* 步骤3：幂等处理，未启动时仅确保比较寄存器为0。 */
    if (!ctx->started)
    {
        __HAL_TIM_SET_COMPARE(ctx->htim, ctx->channel, 0U);
        ctx->duty_last = 0U;
        return DRV_PWM_STATUS_OK;
    }

    /* 步骤4：调用 HAL 停止通道。 */
    if (HAL_TIM_PWM_Stop(ctx->htim, ctx->channel) != HAL_OK)
    {
        return DRV_PWM_STATUS_HAL_FAIL;
    }

    /* 步骤5：停机后强制清零输出缓存和寄存器。 */
    __HAL_TIM_SET_COMPARE(ctx->htim, ctx->channel, 0U);
    ctx->duty_last = 0U;
    ctx->started = false;

    return DRV_PWM_STATUS_OK;
}

/**
 * @brief 设置 PWM 占空比。
 * @param ctx PWM 上下文对象。
 * @param duty 目标占空比。
 * @return drv_pwm_status_e 状态码。
 */
drv_pwm_status_e drv_pwm_set_duty(drv_pwm_ctx_t *ctx, uint16_t duty)
{
    uint16_t duty_limited; /* 限幅后的占空比（未反相） */
    uint16_t duty_output;  /* 最终写入比较寄存器的占空比 */

    /* 步骤1：空指针校验。 */
    if (ctx == NULL)
    {
        return DRV_PWM_STATUS_INVALID_PARAM;
    }

    /* 步骤2：要求上下文已初始化且 PWM 已启动。 */
    if ((!drv_pwm_is_ctx_inited(ctx)) || (!ctx->started))
    {
        return DRV_PWM_STATUS_INVALID_STATE;
    }

    /* 步骤3：先按配置上限限幅，防止比较值越界。 */
    duty_limited = (duty > ctx->duty_max) ? ctx->duty_max : duty;

    /* 步骤4：按反相配置得到最终输出值。 */
    if (ctx->invert)
    {
        duty_output = (uint16_t)(ctx->duty_max - duty_limited);
    }
    else
    {
        duty_output = duty_limited;
    }

    /* 步骤5：写寄存器并更新 last 缓存。 */
    __HAL_TIM_SET_COMPARE(ctx->htim, ctx->channel, duty_output);
    ctx->duty_last = duty_output;

    return DRV_PWM_STATUS_OK;
}

/**
 * @brief 获取占空比上限配置值。
 * @param ctx PWM 上下文对象。
 * @return uint16_t 上限值；ctx 非法返回 0。
 */
uint16_t drv_pwm_get_duty_max(const drv_pwm_ctx_t *ctx)
{
    if (!drv_pwm_is_ctx_inited(ctx))
    {
        return 0U;
    }

    return ctx->duty_max;
}

/**
 * @brief 查询 PWM 是否已启动。
 * @param ctx PWM 上下文对象。
 * @return true 已启动。
 * @return false 未启动或对象非法。
 */
bool drv_pwm_is_started(const drv_pwm_ctx_t *ctx)
{
    return (bool)(drv_pwm_is_ctx_inited(ctx) && ctx->started);
}
