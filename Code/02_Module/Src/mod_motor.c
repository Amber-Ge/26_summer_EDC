#include "mod_motor.h"

#include "drv_gpio.h"
#include "drv_pwm.h"

#include <string.h>

typedef struct
{
    mod_motor_mode_e mode;
    int8_t last_sign;
    uint8_t zero_cross_pending;
    int16_t pending_duty;
    int32_t current_speed;
    int64_t total_position;
    bool hw_ready;
} motor_state_t;

/* 当前生效的硬件映射表 */
static mod_motor_hw_cfg_t s_motor_hw_active[MOD_MOTOR_MAX];
/* 绑定状态标志 */
static bool s_motor_hw_bound = false;

/* 模块运行时状态对象 */
static motor_state_t s_motor_state[MOD_MOTOR_MAX];
static drv_pwm_dev_t s_pwm_dev[MOD_MOTOR_MAX];
static drv_encoder_dev_t s_enc_dev[MOD_MOTOR_MAX];

/**
 * @brief 校验电机ID是否合法
 */
static bool is_valid_motor_id(mod_motor_id_e id)
{
    return ((id >= MOD_MOTOR_LEFT) && (id < MOD_MOTOR_MAX));
}

/**
 * @brief 校验单个映射项是否合法
 */
static bool mod_motor_check_cfg_item(const mod_motor_hw_cfg_t *item)
{
    // 1. 先做空指针保护。
    if (item == NULL)
    {
        return false;
    }

    // 2. GPIO方向控制引脚必须完整。
    if ((item->in1_port == NULL) || (item->in2_port == NULL))
    {
        return false;
    }

    // 3. PWM和编码器的定时器句柄必须有效。
    if ((item->pwm_htim == NULL) || (item->enc_htim == NULL))
    {
        return false;
    }

    // 4. 编码器位宽必须是驱动层支持的16位或32位。
    if ((item->enc_counter_bits != DRV_ENCODER_BITS_16) &&
        (item->enc_counter_bits != DRV_ENCODER_BITS_32))
    {
        return false;
    }

    // 5. 通过全部校验。
    return true;
}

bool mod_motor_bind_map(const mod_motor_hw_cfg_t *map, uint8_t map_num)
{
    // 1. 基础参数校验：指针不能为空，数量必须一致。
    if ((map == NULL) || (map_num != MOD_MOTOR_MAX))
    {
        return false;
    }

    // 2. 逐项校验每个通道映射是否合法。
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        if (!mod_motor_check_cfg_item(&map[i]))
        {
            return false;
        }
    }

    // 3. 拷贝映射并设置绑定成功标志。
    memcpy(s_motor_hw_active, map, sizeof(s_motor_hw_active));
    s_motor_hw_bound = true;

    // 4. 返回成功。
    return true;
}

void mod_motor_unbind_map(void)
{
    // 1. 仅清空绑定标志，映射缓存保留（重新绑定时会覆盖）。
    s_motor_hw_bound = false;
}

bool mod_motor_is_bound(void)
{
    // 1. 直接返回当前绑定状态。
    return s_motor_hw_bound;
}

/**
 * @brief 占空比限幅
 */
static int16_t clamp_duty(int16_t duty)
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
 * @brief 设置TB6612方向引脚电平
 */
static void set_half_bridge(mod_motor_id_e id, gpio_level_e in1_level, gpio_level_e in2_level)
{
    const mod_motor_hw_cfg_t *p_hw = &s_motor_hw_active[id];

    // 1. 设置IN1。
    drv_gpio_write(p_hw->in1_port, p_hw->in1_pin, in1_level);

    // 2. 设置IN2。
    drv_gpio_write(p_hw->in2_port, p_hw->in2_pin, in2_level);
}

/**
 * @brief 施加驱动模式输出
 */
static void apply_drive(mod_motor_id_e id, uint16_t duty, int8_t sign)
{
    // 1. 若硬件未就绪，直接返回。
    if (!s_motor_state[id].hw_ready)
    {
        return;
    }

    // 2. 根据符号决定方向；符号为0时执行滑行输出。
    if (sign > 0)
    {
        set_half_bridge(id, GPIO_LEVEL_HIGH, GPIO_LEVEL_LOW);
    }
    else if (sign < 0)
    {
        set_half_bridge(id, GPIO_LEVEL_LOW, GPIO_LEVEL_HIGH);
    }
    else
    {
        set_half_bridge(id, GPIO_LEVEL_LOW, GPIO_LEVEL_LOW);
        duty = 0U;
    }

    // 3. 下发PWM占空比。
    drv_pwm_set_duty(&s_pwm_dev[id], duty);
}

/**
 * @brief 施加刹车模式输出
 */
static void apply_brake(mod_motor_id_e id)
{
    // 1. TB6612短刹：IN1/IN2同时拉高。
    set_half_bridge(id, GPIO_LEVEL_HIGH, GPIO_LEVEL_HIGH);

    // 2. 若PWM已就绪，拉到最大占空比增强刹车力度。
    if (s_motor_state[id].hw_ready)
    {
        drv_pwm_set_duty(&s_pwm_dev[id], drv_pwm_get_duty_max(&s_pwm_dev[id]));
    }
}

/**
 * @brief 施加滑行模式输出
 */
static void apply_coast(mod_motor_id_e id)
{
    // 1. 滑行模式：IN1/IN2同时拉低。
    set_half_bridge(id, GPIO_LEVEL_LOW, GPIO_LEVEL_LOW);

    // 2. 若PWM已就绪，占空比清零。
    if (s_motor_state[id].hw_ready)
    {
        drv_pwm_set_duty(&s_pwm_dev[id], 0U);
    }
}

/**
 * @brief 真正执行驱动指令（已过零保护后）
 */
static void execute_drive_cmd(mod_motor_id_e id, int16_t duty_cmd)
{
    motor_state_t *p_state = &s_motor_state[id];
    int16_t duty = clamp_duty(duty_cmd);
    int8_t sign = 0;
    uint16_t abs_duty = 0U;

    // 1. 提取方向符号和绝对占空比。
    if (duty > 0)
    {
        sign = 1;
        abs_duty = (uint16_t)duty;
    }
    else if (duty < 0)
    {
        sign = -1;
        abs_duty = (uint16_t)(-duty);
    }

    // 2. 执行驱动。
    apply_drive(id, abs_duty, sign);

    // 3. 记录本次实际输出方向。
    p_state->last_sign = sign;
}

void mod_motor_init(void)
{
    // 1. 强制绑定检查：未绑定时直接把状态标为不可用并退出。
    if (!mod_motor_is_bound())
    {
        memset(s_motor_state, 0, sizeof(s_motor_state));
        return;
    }

    // 2. 遍历所有电机通道执行初始化。
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        const mod_motor_hw_cfg_t *p_hw = &s_motor_hw_active[i];
        motor_state_t *p_state = &s_motor_state[i];
        bool pwm_ok;
        bool enc_ok;

        // 2.1 先清空状态机运行状态。
        p_state->mode = MOTOR_MODE_COAST;
        p_state->last_sign = 0;
        p_state->zero_cross_pending = 0U;
        p_state->pending_duty = 0;
        p_state->current_speed = 0;
        p_state->total_position = 0;
        p_state->hw_ready = false;

        // 2.2 初始化并启动PWM。
        pwm_ok = drv_pwm_device_init(&s_pwm_dev[i],
                                     p_hw->pwm_htim,
                                     p_hw->pwm_channel,
                                     MOD_MOTOR_DUTY_MAX,
                                     p_hw->pwm_invert);
        if (pwm_ok)
        {
            pwm_ok = drv_pwm_start(&s_pwm_dev[i]);
        }

        // 2.3 初始化并启动编码器。
        enc_ok = drv_encoder_device_init(&s_enc_dev[i],
                                         p_hw->enc_htim,
                                         p_hw->enc_counter_bits,
                                         p_hw->enc_invert);
        if (enc_ok)
        {
            enc_ok = drv_encoder_start(&s_enc_dev[i]);
        }

        // 2.4 只有两项都成功才算硬件就绪。
        p_state->hw_ready = (bool)(pwm_ok && enc_ok);

        // 2.5 上电默认进入滑行安全态。
        apply_coast((mod_motor_id_e)i);
    }
}

void mod_motor_set_mode(mod_motor_id_e id, mod_motor_mode_e mode)
{
    // 1. ID非法直接返回。
    if (!is_valid_motor_id(id))
    {
        return;
    }

    // 2. 模块未绑定时直接返回。
    if (!mod_motor_is_bound())
    {
        return;
    }

    // 3. 记录模式并清除过零挂起标志。
    s_motor_state[id].mode = mode;
    s_motor_state[id].zero_cross_pending = 0U;

    // 4. 根据模式直接施加输出。
    if (mode == MOTOR_MODE_BRAKE)
    {
        apply_brake(id);
    }
    else if (mode == MOTOR_MODE_COAST)
    {
        apply_coast(id);
    }
}

void mod_motor_set_duty(mod_motor_id_e id, int16_t duty)
{
    motor_state_t *p_state;
    int16_t duty_clamped;
    int8_t current_sign = 0;

    // 1. ID非法直接返回。
    if (!is_valid_motor_id(id))
    {
        return;
    }

    // 2. 模块未绑定直接返回。
    if (!mod_motor_is_bound())
    {
        return;
    }

    p_state = &s_motor_state[id];

    // 3. 硬件未就绪或模式非DRIVE时，不响应占空比命令。
    if (!(p_state->hw_ready && (p_state->mode == MOTOR_MODE_DRIVE)))
    {
        return;
    }

    // 4. 先限幅，再提取方向符号。
    duty_clamped = clamp_duty(duty);
    if (duty_clamped > 0)
    {
        current_sign = 1;
    }
    else if (duty_clamped < 0)
    {
        current_sign = -1;
    }

    // 5. 若发生反向切换，执行过零保护。
    if ((p_state->last_sign != 0) && (current_sign != 0) && (p_state->last_sign != current_sign))
    {
        apply_coast(id);
        p_state->zero_cross_pending = 1U;
        p_state->pending_duty = duty_clamped;
        p_state->last_sign = current_sign;
    }
    else
    {
        // 6. 同向或静止启动，直接执行；若处于挂起期，仅更新挂起值。
        if (p_state->zero_cross_pending == 0U)
        {
            execute_drive_cmd(id, duty_clamped);
        }
        else
        {
            p_state->pending_duty = duty_clamped;
        }
    }
}

void mod_motor_tick(void)
{
    // 1. 模块未绑定时直接返回。
    if (!mod_motor_is_bound())
    {
        return;
    }

    // 2. 遍历所有通道刷新状态。
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        motor_state_t *p_state = &s_motor_state[i];

        // 2.1 硬件就绪时更新速度和位置。
        if (p_state->hw_ready)
        {
            p_state->current_speed = drv_encoder_get_delta(&s_enc_dev[i]);
            p_state->total_position += p_state->current_speed;

            // 2.2 处理过零挂起命令。
            if (p_state->zero_cross_pending != 0U)
            {
                p_state->zero_cross_pending = 0U;
                if (p_state->mode == MOTOR_MODE_DRIVE)
                {
                    execute_drive_cmd((mod_motor_id_e)i, p_state->pending_duty);
                }
            }
        }
        else
        {
            // 2.3 硬件未就绪时速度读数强制归零。
            p_state->current_speed = 0;
        }
    }
}

int32_t mod_motor_get_speed(mod_motor_id_e id)
{
    // 1. ID非法直接返回0。
    if (!is_valid_motor_id(id))
    {
        return 0;
    }

    // 2. 模块未绑定直接返回0。
    if (!mod_motor_is_bound())
    {
        return 0;
    }

    // 3. 返回缓存速度。
    return s_motor_state[id].current_speed;
}

int64_t mod_motor_get_position(mod_motor_id_e id)
{
    // 1. ID非法直接返回0。
    if (!is_valid_motor_id(id))
    {
        return 0;
    }

    // 2. 模块未绑定直接返回0。
    if (!mod_motor_is_bound())
    {
        return 0;
    }

    // 3. 返回累计位置。
    return s_motor_state[id].total_position;
}