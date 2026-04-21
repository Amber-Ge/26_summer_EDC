/**
 * @file    mod_motor.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   双电机模块实现（ctx 架构，无兼容接口）。
 * @details
 * 1. 实现双电机硬件映射绑定、方向控制、PWM 输出与编码器反馈更新。
 * 2. 通过统一 ctx 生命周期管理，保证初始化、运行、解绑过程可预测。
 * 3. 运行期内置过零保护机制，避免正反切换时桥臂直接反灌。
 */

#include "mod_motor.h"

#include "drv_gpio.h"

#include <string.h>

/* 默认上下文：供任务层直接使用。 */
static mod_motor_ctx_t s_default_ctx;

/**
 * @brief 校验电机通道 ID。
 */
static bool mod_motor_is_valid_id(mod_motor_id_e id)
{
    return ((id >= MOD_MOTOR_LEFT) && (id < MOD_MOTOR_MAX));
}

/**
 * @brief 判断上下文是否可运行。
 */
static bool mod_motor_is_ctx_ready(const mod_motor_ctx_t *ctx)
{
    return ((ctx != NULL) && ctx->inited && ctx->bound);
}

/**
 * @brief 校验单路硬件映射项是否完整。
 * @details
 * 该函数仅检查结构体字段合法性，不访问外设，不触发任何硬件动作。
 */
static bool mod_motor_check_cfg_item(const mod_motor_hw_cfg_t *item)
{
    if (item == NULL)
    {
        return false;
    }

    /* 方向桥臂引脚必须完整。 */
    if ((item->in1.port == NULL) || (item->in2.port == NULL))
    {
        return false;
    }

    /* PWM/编码器定时器句柄必须有效。 */
    if ((item->pwm.instance == NULL) || (item->encoder.instance == NULL))
    {
        return false;
    }

    /* 编码器位宽只允许驱动层支持的 16 位或 32 位。 */
    if ((item->encoder.counter_bits != DRV_ENCODER_BITS_16) &&
        (item->encoder.counter_bits != DRV_ENCODER_BITS_32))
    {
        return false;
    }

    if (item->pwm.duty_max == 0U)
    {
        return false;
    }

    return true;
}

/**
 * @brief duty 限幅到 [-MOD_MOTOR_DUTY_MAX, MOD_MOTOR_DUTY_MAX]。
 */
static int16_t mod_motor_clamp_duty(int16_t duty)
{
    if (duty > (int16_t)MOD_MOTOR_DUTY_MAX)
    {
        return (int16_t)MOD_MOTOR_DUTY_MAX;
    }

    if (duty < -(int16_t)MOD_MOTOR_DUTY_MAX)
    {
        return -(int16_t)MOD_MOTOR_DUTY_MAX;
    }

    return duty;
}

/**
 * @brief 写入 TB6612 桥臂方向引脚。
 * @param ctx 模块上下文。
 * @param id 通道 ID。
 * @param in1_level IN1 电平。
 * @param in2_level IN2 电平。
 */
static void mod_motor_set_half_bridge(mod_motor_ctx_t *ctx,
                                      mod_motor_id_e id,
                                      gpio_level_e in1_level,
                                      gpio_level_e in2_level)
{
    const mod_motor_hw_cfg_t *p_hw = &ctx->map[id];

    drv_gpio_write_pin(&p_hw->in1, in1_level);
    drv_gpio_write_pin(&p_hw->in2, in2_level);
}

/**
 * @brief 安全写 PWM duty。
 * @details
 * 任何一次底层写入失败都会把通道降级为 `hw_ready=false`，
 * 后续周期会停止对该通道进行主动驱动，避免持续输出异常。
 */
static void mod_motor_write_pwm_duty_safe(mod_motor_ctx_t *ctx, mod_motor_id_e id, uint16_t duty)
{
    if (drv_pwm_set_duty(&ctx->pwm_ctx[id], duty) != DRV_PWM_STATUS_OK)
    {
        ctx->state[id].hw_ready = false;
    }
}

/**
 * @brief 应用 DRIVE 模式输出。
 * @param ctx 模块上下文。
 * @param id 通道 ID。
 * @param duty 绝对占空比。
 * @param sign 方向符号（-1/0/1）。
 */
static void mod_motor_apply_drive(mod_motor_ctx_t *ctx, mod_motor_id_e id, uint16_t duty, int8_t sign)
{
    if (!ctx->state[id].hw_ready)
    {
        return;
    }

    /* 正反向通过桥臂电平决定，零速时桥臂释放并强制 duty=0。 */
    if (sign > 0)
    {
        mod_motor_set_half_bridge(ctx, id, GPIO_LEVEL_HIGH, GPIO_LEVEL_LOW);
    }
    else if (sign < 0)
    {
        mod_motor_set_half_bridge(ctx, id, GPIO_LEVEL_LOW, GPIO_LEVEL_HIGH);
    }
    else
    {
        mod_motor_set_half_bridge(ctx, id, GPIO_LEVEL_LOW, GPIO_LEVEL_LOW);
        duty = 0U;
    }

    mod_motor_write_pwm_duty_safe(ctx, id, duty);
}

/**
 * @brief 应用 BRAKE 模式输出。
 */
static void mod_motor_apply_brake(mod_motor_ctx_t *ctx, mod_motor_id_e id)
{
    mod_motor_set_half_bridge(ctx, id, GPIO_LEVEL_HIGH, GPIO_LEVEL_HIGH);

    if (ctx->state[id].hw_ready)
    {
        /* 刹车模式下 PWM 拉到最大，提升制动力。 */
        mod_motor_write_pwm_duty_safe(ctx, id, drv_pwm_get_duty_max(&ctx->pwm_ctx[id]));
    }
}

/**
 * @brief 应用 COAST 模式输出。
 */
static void mod_motor_apply_coast(mod_motor_ctx_t *ctx, mod_motor_id_e id)
{
    mod_motor_set_half_bridge(ctx, id, GPIO_LEVEL_LOW, GPIO_LEVEL_LOW);

    if (ctx->state[id].hw_ready)
    {
        mod_motor_write_pwm_duty_safe(ctx, id, 0U);
    }
}

/**
 * @brief 执行 DRIVE 命令（已过过零保护判定）。
 * @param ctx 模块上下文。
 * @param id 通道 ID。
 * @param duty_cmd 输入 duty 命令（可正可负）。
 */
static void mod_motor_execute_drive_cmd(mod_motor_ctx_t *ctx, mod_motor_id_e id, int16_t duty_cmd)
{
    mod_motor_channel_state_t *p_state = &ctx->state[id];
    int16_t duty_limited = mod_motor_clamp_duty(duty_cmd);
    int8_t sign = 0;
    uint16_t abs_duty = 0U;

    /* 把有符号 duty 分解为方向符号 + 无符号绝对值。 */
    if (duty_limited > 0)
    {
        sign = 1;
        abs_duty = (uint16_t)duty_limited;
    }
    else if (duty_limited < 0)
    {
        sign = -1;
        abs_duty = (uint16_t)(-duty_limited);
    }

    mod_motor_apply_drive(ctx, id, abs_duty, sign);
    p_state->last_sign = sign;
}

mod_motor_ctx_t *mod_motor_get_default_ctx(void)
{
    return &s_default_ctx;
}

bool mod_motor_ctx_init(mod_motor_ctx_t *ctx, const mod_motor_bind_t *bind)
{
    if (ctx == NULL)
    {
        return false;
    }

    /* 全量清零后置 inited=true，确保生命周期从干净状态开始。 */
    memset(ctx, 0, sizeof(*ctx));
    ctx->inited = true;

    /* 支持“初始化即绑定”模式，便于 InitTask 一次性装配。 */
    if (bind != NULL)
    {
        return mod_motor_bind(ctx, bind);
    }

    return true;
}

void mod_motor_ctx_deinit(mod_motor_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    mod_motor_unbind(ctx);
    memset(ctx, 0, sizeof(*ctx));
}

bool mod_motor_bind(mod_motor_ctx_t *ctx, const mod_motor_bind_t *bind)
{
    if ((ctx == NULL) || (!ctx->inited) || (bind == NULL) ||
        (bind->map == NULL) || (bind->map_num != MOD_MOTOR_MAX))
    {
        return false;
    }

    /* 先全量校验映射项，保证绑定过程原子性。 */
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        if (!mod_motor_check_cfg_item(&bind->map[i]))
        {
            return false;
        }
    }

    /* 若已绑定旧映射，先做完整解绑。 */
    if (ctx->bound)
    {
        mod_motor_unbind(ctx);
    }

    memcpy(ctx->map, bind->map, sizeof(ctx->map));
    ctx->bound = true;

    return true;
}

void mod_motor_unbind(mod_motor_ctx_t *ctx)
{
    if ((ctx == NULL) || (!ctx->inited))
    {
        return;
    }

    /* 解绑前尝试停掉所有通道，避免残留输出。 */
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        (void)drv_pwm_stop(&ctx->pwm_ctx[i]);
        (void)drv_encoder_stop(&ctx->enc_ctx[i]);
    }

    memset(ctx->map, 0, sizeof(ctx->map));
    memset(ctx->state, 0, sizeof(ctx->state));
    memset(ctx->pwm_ctx, 0, sizeof(ctx->pwm_ctx));
    memset(ctx->enc_ctx, 0, sizeof(ctx->enc_ctx));
    ctx->bound = false;
}

bool mod_motor_is_bound(const mod_motor_ctx_t *ctx)
{
    return mod_motor_is_ctx_ready(ctx);
}

void mod_motor_init(mod_motor_ctx_t *ctx)
{
    if ((ctx == NULL) || (!ctx->inited))
    {
        return;
    }

    if (!mod_motor_is_ctx_ready(ctx))
    {
        memset(ctx->state, 0, sizeof(ctx->state));
        return;
    }

    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        const mod_motor_hw_cfg_t *p_hw = &ctx->map[i];
        mod_motor_channel_state_t *p_state = &ctx->state[i];
        drv_pwm_status_e pwm_status;
        drv_encoder_status_e enc_status;
        bool pwm_ok;
        bool enc_ok;

        /* 每次 init 都重建通道运行态。 */
        p_state->mode = MOTOR_MODE_COAST;
        p_state->last_sign = 0;
        p_state->zero_cross_pending = 0U;
        p_state->pending_duty = 0;
        p_state->current_speed = 0;
        p_state->total_position = 0;
        p_state->hw_ready = false;

        /* 初始化并启动 PWM 通道。 */
        pwm_status = drv_pwm_ctx_init(&ctx->pwm_ctx[i],
                                      &p_hw->pwm);
        if (pwm_status == DRV_PWM_STATUS_OK)
        {
            pwm_status = drv_pwm_start(&ctx->pwm_ctx[i]);
        }
        pwm_ok = (pwm_status == DRV_PWM_STATUS_OK);

        /* 初始化并启动编码器通道。 */
        enc_status = drv_encoder_ctx_init(&ctx->enc_ctx[i],
                                          &p_hw->encoder);
        if (enc_status == DRV_ENCODER_STATUS_OK)
        {
            enc_status = drv_encoder_start(&ctx->enc_ctx[i]);
        }
        enc_ok = (enc_status == DRV_ENCODER_STATUS_OK);

        /* 仅当 PWM 与编码器都正常时才标记通道可用。 */
        p_state->hw_ready = (bool)(pwm_ok && enc_ok);

        /* 上电后统一进入滑行安全态。 */
        mod_motor_apply_coast(ctx, (mod_motor_id_e)i);
    }
}

void mod_motor_set_mode(mod_motor_ctx_t *ctx, mod_motor_id_e id, mod_motor_mode_e mode)
{
    if (!mod_motor_is_valid_id(id))
    {
        return;
    }

    if (!mod_motor_is_ctx_ready(ctx))
    {
        return;
    }

    ctx->state[id].mode = mode;
    ctx->state[id].zero_cross_pending = 0U;

    if (mode == MOTOR_MODE_BRAKE)
    {
        mod_motor_apply_brake(ctx, id);
    }
    else if (mode == MOTOR_MODE_COAST)
    {
        mod_motor_apply_coast(ctx, id);
    }
    else
    {
        /* DRIVE 模式只切状态，不立刻改桥臂，等待后续 duty 命令驱动。 */
    }
}

void mod_motor_set_duty(mod_motor_ctx_t *ctx, mod_motor_id_e id, int16_t duty)
{
    mod_motor_channel_state_t *p_state;
    int16_t duty_limited;
    int8_t current_sign = 0;

    if (!mod_motor_is_valid_id(id))
    {
        return;
    }

    if (!mod_motor_is_ctx_ready(ctx))
    {
        return;
    }

    p_state = &ctx->state[id];

    /* 非 DRIVE 模式或硬件异常时，拒绝处理 duty 命令。 */
    if (!(p_state->hw_ready && (p_state->mode == MOTOR_MODE_DRIVE)))
    {
        return;
    }

    duty_limited = mod_motor_clamp_duty(duty);

    /* 解析当前命令方向符号。 */
    if (duty_limited > 0)
    {
        current_sign = 1;
    }
    else if (duty_limited < 0)
    {
        current_sign = -1;
    }

    /*
     * 过零保护：
     * 当上一周期已有方向且本次方向相反时，先 COAST 一周期，
     * 并把目标命令挂起到 pending_duty，下一次 tick 再执行。
     */
    if ((p_state->last_sign != 0) && (current_sign != 0) && (p_state->last_sign != current_sign))
    {
        mod_motor_apply_coast(ctx, id);
        p_state->zero_cross_pending = 1U;
        p_state->pending_duty = duty_limited;
        p_state->last_sign = current_sign;
    }
    else
    {
        if (p_state->zero_cross_pending == 0U)
        {
            mod_motor_execute_drive_cmd(ctx, id, duty_limited);
        }
        else
        {
            /* 已在过零窗口内，更新挂起命令，等待 tick 统一释放。 */
            p_state->pending_duty = duty_limited;
        }
    }
}

void mod_motor_tick(mod_motor_ctx_t *ctx)
{
    if (!mod_motor_is_ctx_ready(ctx))
    {
        return;
    }

    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        mod_motor_channel_state_t *p_state = &ctx->state[i];
        int32_t encoder_delta = 0;

        if (!p_state->hw_ready)
        {
            p_state->current_speed = 0;
            continue;
        }

        /* 读取编码器增量并更新速度/位置反馈。 */
        if (drv_encoder_get_delta(&ctx->enc_ctx[i], &encoder_delta) == DRV_ENCODER_STATUS_OK)
        {
            p_state->current_speed = encoder_delta;
            p_state->total_position += (int64_t)p_state->current_speed;
        }
        else
        {
            /* 反馈链路失效后，降级为不可用通道。 */
            p_state->hw_ready = false;
            p_state->current_speed = 0;
            continue;
        }

        /* 释放过零保护挂起命令（仅 DRIVE 模式下生效）。 */
        if (p_state->zero_cross_pending != 0U)
        {
            p_state->zero_cross_pending = 0U;
            if (p_state->mode == MOTOR_MODE_DRIVE)
            {
                mod_motor_execute_drive_cmd(ctx, (mod_motor_id_e)i, p_state->pending_duty);
            }
        }
    }
}

int32_t mod_motor_get_speed(const mod_motor_ctx_t *ctx, mod_motor_id_e id)
{
    if (!mod_motor_is_valid_id(id))
    {
        return 0;
    }

    if (!mod_motor_is_ctx_ready(ctx))
    {
        return 0;
    }

    return ctx->state[id].current_speed;
}

int64_t mod_motor_get_position(const mod_motor_ctx_t *ctx, mod_motor_id_e id)
{
    if (!mod_motor_is_valid_id(id))
    {
        return 0;
    }

    if (!mod_motor_is_ctx_ready(ctx))
    {
        return 0;
    }

    return ctx->state[id].total_position;
}

