/**
 * @file    mod_motor.c
 * @brief   双电机模块实现。
 * @details
 * 1. 文件作用：实现双电机硬件绑定、方向控制、占空比输出和反馈量更新。
 * 2. 解耦边界：本文件只实现执行机构抽象，不包含 PID 参数整定和控制策略决策。
 * 3. 上层绑定：`DccTask` 周期调用速度/位置读取与占空比写入接口形成闭环。
 * 4. 下层依赖：`drv_pwm` 输出功率、`drv_gpio` 控制方向、`drv_encoder` 采集计数反馈。
 */

#include "mod_motor.h"

#include "drv_gpio.h"
#include "drv_pwm.h"

#include <string.h>

/**
 * @brief 电机通道运行时状态。
 */
typedef struct
{
    mod_motor_mode_e mode;      // 当前运行模式
    int8_t last_sign;           // 上一次已执行方向符号（-1/0/1）
    uint8_t zero_cross_pending; // 过零保护挂起标志（0/1）
    int16_t pending_duty;       // 过零保护期间缓存的占空比命令
    int32_t current_speed;      // 当前速度增量
    int64_t total_position;     // 累计位置值
    bool hw_ready;              // 当前通道硬件是否就绪
} motor_state_t;

static mod_motor_hw_cfg_t s_motor_hw_active[MOD_MOTOR_MAX]; // 当前生效的硬件映射表
static bool s_motor_hw_bound = false;                       // 硬件映射绑定状态

static motor_state_t s_motor_state[MOD_MOTOR_MAX];          // 各通道运行时状态
static drv_pwm_dev_t s_pwm_dev[MOD_MOTOR_MAX];              // 各通道 PWM 设备对象
static drv_encoder_dev_t s_enc_dev[MOD_MOTOR_MAX];          // 各通道编码器设备对象

/**
 * @brief 校验电机通道ID是否合法。
 * @param id 电机通道ID。
 * @return true ID 在有效范围内。
 * @return false ID 越界。
 */
static bool is_valid_motor_id(mod_motor_id_e id)
{
    // 1. 判断通道ID是否位于定义范围 [MOD_MOTOR_LEFT, MOD_MOTOR_MAX)。
    return ((id >= MOD_MOTOR_LEFT) && (id < MOD_MOTOR_MAX));
}

/**
 * @brief 校验单个硬件映射项是否合法。
 * @param item 映射项指针。
 * @return true 映射项合法。
 * @return false 映射项非法。
 */
static bool mod_motor_check_cfg_item(const mod_motor_hw_cfg_t *item)
{
    // 1. 先做空指针保护。
    if (item == NULL)
    {
        return false;
    }

    // 2. GPIO 方向控制引脚必须完整。
    if ((item->in1_port == NULL) || (item->in2_port == NULL))
    {
        return false;
    }

    // 3. PWM 和编码器的定时器句柄必须有效。
    if ((item->pwm_htim == NULL) || (item->enc_htim == NULL))
    {
        return false;
    }

    // 4. 编码器位宽必须是驱动层支持的 16 位或 32 位。
    if ((item->enc_counter_bits != DRV_ENCODER_BITS_16) &&
        (item->enc_counter_bits != DRV_ENCODER_BITS_32))
    {
        return false;
    }

    // 5. 通过全部校验。
    return true;
}

/**
 * @brief 绑定电机硬件映射表。
 * @param map 映射表首地址。
 * @param map_num 映射项数量。
 * @return true 绑定成功。
 * @return false 参数非法或映射校验失败。
 */
bool mod_motor_bind_map(const mod_motor_hw_cfg_t *map, uint8_t map_num)
{
    // 1. 基础参数校验：指针不能为空，数量必须一致。
    if ((map == NULL) || (map_num != MOD_MOTOR_MAX))
    {
        return false;
    }

    // 2. 逐项校验每个通道映射是否合法。
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++) // i: 电机通道索引
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

/**
 * @brief 解绑电机硬件映射表。
 * @return 无返回值。
 */
void mod_motor_unbind_map(void)
{
    // 1. 仅清空绑定标志，映射缓存保留（重新绑定时会覆盖）。
    s_motor_hw_bound = false;
}

/**
 * @brief 查询电机模块是否已绑定。
 * @return true 已绑定。
 * @return false 未绑定。
 */
bool mod_motor_is_bound(void)
{
    // 1. 直接返回当前绑定状态。
    return s_motor_hw_bound;
}

/**
 * @brief 对占空比做上下限裁剪。
 * @param duty 原始占空比命令。
 * @return int16_t 裁剪后的占空比。
 */
static int16_t clamp_duty(int16_t duty)
{
    // 1. 上限保护：超过最大占空比时截断到上限。
    if (duty > (int16_t)MOD_MOTOR_DUTY_MAX)
    {
        return (int16_t)MOD_MOTOR_DUTY_MAX;
    }

    // 2. 下限保护：低于负最大占空比时截断到下限。
    if (duty < -(int16_t)MOD_MOTOR_DUTY_MAX)
    {
        return -(int16_t)MOD_MOTOR_DUTY_MAX;
    }

    // 3. 在有效范围内时原样返回。
    return duty;
}

/**
 * @brief 设置 TB6612 半桥方向引脚电平。
 * @param id 电机通道ID。
 * @param in1_level IN1 目标逻辑电平。
 * @param in2_level IN2 目标逻辑电平。
 * @return 无返回值。
 */
static void set_half_bridge(mod_motor_id_e id, gpio_level_e in1_level, gpio_level_e in2_level)
{
    const mod_motor_hw_cfg_t *p_hw = &s_motor_hw_active[id]; // 当前通道硬件映射

    // 1. 设置 IN1 电平。
    drv_gpio_write(p_hw->in1_port, p_hw->in1_pin, in1_level);

    // 2. 设置 IN2 电平。
    drv_gpio_write(p_hw->in2_port, p_hw->in2_pin, in2_level);
}

/**
 * @brief 施加驱动模式输出。
 * @param id 电机通道ID。
 * @param duty 目标占空比（绝对值）。
 * @param sign 方向符号（-1/0/1）。
 * @return 无返回值。
 */
static void apply_drive(mod_motor_id_e id, uint16_t duty, int8_t sign)
{
    // 1. 若硬件未就绪，直接返回。
    if (!s_motor_state[id].hw_ready)
    {
        return;
    }

    // 2. 根据符号决定方向；符号为 0 时执行滑行输出。
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

    // 3. 下发 PWM 占空比。
    drv_pwm_set_duty(&s_pwm_dev[id], duty);
}

/**
 * @brief 施加刹车模式输出。
 * @param id 电机通道ID。
 * @return 无返回值。
 */
static void apply_brake(mod_motor_id_e id)
{
    // 1. TB6612 短刹：IN1/IN2 同时拉高。
    set_half_bridge(id, GPIO_LEVEL_HIGH, GPIO_LEVEL_HIGH);

    // 2. 若 PWM 已就绪，拉到最大占空比增强刹车力度。
    if (s_motor_state[id].hw_ready)
    {
        drv_pwm_set_duty(&s_pwm_dev[id], drv_pwm_get_duty_max(&s_pwm_dev[id]));
    }
}

/**
 * @brief 施加滑行模式输出。
 * @param id 电机通道ID。
 * @return 无返回值。
 */
static void apply_coast(mod_motor_id_e id)
{
    // 1. 滑行模式：IN1/IN2 同时拉低。
    set_half_bridge(id, GPIO_LEVEL_LOW, GPIO_LEVEL_LOW);

    // 2. 若 PWM 已就绪，占空比清零。
    if (s_motor_state[id].hw_ready)
    {
        drv_pwm_set_duty(&s_pwm_dev[id], 0U);
    }
}

/**
 * @brief 执行驱动占空比命令（过零保护后的实际执行入口）。
 * @param id 电机通道ID。
 * @param duty_cmd 原始占空比命令。
 * @return 无返回值。
 */
static void execute_drive_cmd(mod_motor_id_e id, int16_t duty_cmd)
{
    motor_state_t *p_state = &s_motor_state[id]; // 当前通道运行状态
    int16_t duty = clamp_duty(duty_cmd);         // 限幅后的占空比
    int8_t sign = 0;                             // 方向符号（-1/0/1）
    uint16_t abs_duty = 0U;                      // 绝对值占空比

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

    // 2. 执行驱动输出。
    apply_drive(id, abs_duty, sign);

    // 3. 记录本次实际输出方向。
    p_state->last_sign = sign;
}

/**
 * @brief 初始化电机模块。
 * @return 无返回值。
 */
void mod_motor_init(void)
{
    // 1. 强制绑定检查：未绑定时直接清空状态并退出。
    if (!mod_motor_is_bound())
    {
        memset(s_motor_state, 0, sizeof(s_motor_state));
        return;
    }

    // 2. 遍历所有电机通道执行初始化。
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++) // i: 电机通道索引
    {
        const mod_motor_hw_cfg_t *p_hw = &s_motor_hw_active[i]; // 当前通道硬件映射
        motor_state_t *p_state = &s_motor_state[i];             // 当前通道运行状态
        bool pwm_ok;                                             // PWM 初始化与启动结果
        bool enc_ok;                                             // 编码器初始化与启动结果

        // 2.1 先清空状态机运行状态。
        p_state->mode = MOTOR_MODE_COAST;
        p_state->last_sign = 0;
        p_state->zero_cross_pending = 0U;
        p_state->pending_duty = 0;
        p_state->current_speed = 0;
        p_state->total_position = 0;
        p_state->hw_ready = false;

        // 2.2 初始化并启动 PWM。
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

/**
 * @brief 设置电机运行模式。
 * @param id 电机通道ID。
 * @param mode 目标运行模式。
 * @return 无返回值。
 */
void mod_motor_set_mode(mod_motor_id_e id, mod_motor_mode_e mode)
{
    // 1. ID 非法直接返回。
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

/**
 * @brief 设置电机占空比。
 * @param id 电机通道ID。
 * @param duty 目标占空比（支持正反方向）。
 * @return 无返回值。
 */
void mod_motor_set_duty(mod_motor_id_e id, int16_t duty)
{
    motor_state_t *p_state;      // 当前通道运行状态指针
    int16_t duty_clamped;        // 限幅后的占空比命令
    int8_t current_sign = 0;     // 当前命令方向符号（-1/0/1）

    // 1. ID 非法直接返回。
    if (!is_valid_motor_id(id))
    {
        return;
    }

    // 2. 模块未绑定直接返回。
    if (!mod_motor_is_bound())
    {
        return;
    }

    // 3. 绑定当前通道状态对象。
    p_state = &s_motor_state[id];

    // 4. 硬件未就绪或模式非 DRIVE 时，不响应占空比命令。
    if (!(p_state->hw_ready && (p_state->mode == MOTOR_MODE_DRIVE)))
    {
        return;
    }

    // 5. 先限幅，再提取方向符号。
    duty_clamped = clamp_duty(duty);
    if (duty_clamped > 0)
    {
        current_sign = 1;
    }
    else if (duty_clamped < 0)
    {
        current_sign = -1;
    }

    // 6. 若发生反向切换，执行过零保护。
    if ((p_state->last_sign != 0) && (current_sign != 0) && (p_state->last_sign != current_sign))
    {
        apply_coast(id);
        p_state->zero_cross_pending = 1U;
        p_state->pending_duty = duty_clamped;
        p_state->last_sign = current_sign;
    }
    else
    {
        // 7. 同向或静止启动直接执行；若处于挂起期，仅更新挂起值。
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

/**
 * @brief 电机周期更新入口。
 * @return 无返回值。
 */
void mod_motor_tick(void)
{
    // 1. 模块未绑定时直接返回。
    if (!mod_motor_is_bound())
    {
        return;
    }

    // 2. 遍历所有通道刷新状态。
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++) // i: 电机通道索引
    {
        motor_state_t *p_state = &s_motor_state[i]; // 当前通道运行状态

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

/**
 * @brief 获取当前速度。
 * @param id 电机通道ID。
 * @return int32_t 当前速度；非法参数或未绑定时返回 0。
 */
int32_t mod_motor_get_speed(mod_motor_id_e id)
{
    // 1. ID 非法直接返回 0。
    if (!is_valid_motor_id(id))
    {
        return 0;
    }

    // 2. 模块未绑定直接返回 0。
    if (!mod_motor_is_bound())
    {
        return 0;
    }

    // 3. 返回缓存速度。
    return s_motor_state[id].current_speed;
}

/**
 * @brief 获取累计位置。
 * @param id 电机通道ID。
 * @return int64_t 累计位置；非法参数或未绑定时返回 0。
 */
int64_t mod_motor_get_position(mod_motor_id_e id)
{
    // 1. ID 非法直接返回 0。
    if (!is_valid_motor_id(id))
    {
        return 0;
    }

    // 2. 模块未绑定直接返回 0。
    if (!mod_motor_is_bound())
    {
        return 0;
    }

    // 3. 返回累计位置。
    return s_motor_state[id].total_position;
}
