/**
 * @file    mod_motor.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   双电机模块接口定义（ctx 架构，无兼容接口）。
 * @details
 * 1. 模块职责：把“方向 GPIO + PWM + 编码器”组合成统一电机控制语义。
 * 2. 模块边界：只负责执行机构抽象，不承担 PID、轨迹规划、模式调度策略。
 * 3. 上层通过 `mod_motor_ctx_t` 使用模块能力，下层通过 `drv_gpio/drv_pwm/drv_encoder` 访问硬件。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_MOTOR_H
#define FINAL_GRADUATE_WORK_MOD_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_encoder.h"
#include "drv_pwm.h"
#include "main.h"

/* 电机绝对占空比上限（与 drv_pwm duty_max 对齐） */
#define MOD_MOTOR_DUTY_MAX (1000U)

/**
 * @brief 电机逻辑通道 ID。
 */
typedef enum
{
    MOD_MOTOR_LEFT = 0, /* 左电机 */
    MOD_MOTOR_RIGHT,    /* 右电机 */
    MOD_MOTOR_MAX       /* 通道总数 */
} mod_motor_id_e;

/**
 * @brief 电机运行模式。
 */
typedef enum
{
    MOTOR_MODE_DRIVE = 0, /* 驱动模式：允许按 duty 输出 */
    MOTOR_MODE_BRAKE,     /* 刹车模式：双桥臂拉高 */
    MOTOR_MODE_COAST      /* 滑行模式：双桥臂释放 */
} mod_motor_mode_e;

/**
 * @brief 单路电机硬件映射配置。
 */
typedef struct
{
    GPIO_TypeDef *in1_port;      /* TB6612 IN1 端口 */
    uint16_t in1_pin;            /* TB6612 IN1 引脚 */
    GPIO_TypeDef *in2_port;      /* TB6612 IN2 端口 */
    uint16_t in2_pin;            /* TB6612 IN2 引脚 */

    TIM_HandleTypeDef *pwm_htim; /* PWM 定时器句柄 */
    uint32_t pwm_channel;        /* PWM 通道号 */
    bool pwm_invert;             /* 占空比反相开关 */

    TIM_HandleTypeDef *enc_htim; /* 编码器定时器句柄 */
    uint8_t enc_counter_bits;    /* 编码器计数器位宽 */
    bool enc_invert;             /* 编码器方向反相开关 */
} mod_motor_hw_cfg_t;

/**
 * @brief 模块绑定参数。
 */
typedef struct
{
    const mod_motor_hw_cfg_t *map; /* 通道映射表首地址 */
    uint8_t map_num;               /* 映射数量，必须等于 MOD_MOTOR_MAX */
} mod_motor_bind_t;

/**
 * @brief 单路电机运行时状态。
 */
typedef struct
{
    mod_motor_mode_e mode;      /* 当前模式 */
    int8_t last_sign;           /* 最近一次已执行方向符号（-1/0/1） */
    uint8_t zero_cross_pending; /* 过零保护挂起标志 */
    int16_t pending_duty;       /* 挂起期间缓存的 duty 命令 */
    int32_t current_speed;      /* 当前周期速度反馈（编码器 delta） */
    int64_t total_position;     /* 累计位置 */
    bool hw_ready;              /* 通道硬件就绪标志 */
} mod_motor_channel_state_t;

/**
 * @brief 模块运行上下文。
 */
typedef struct
{
    bool inited;                                    /* 上下文初始化标志 */
    bool bound;                                     /* 映射绑定标志 */
    mod_motor_hw_cfg_t map[MOD_MOTOR_MAX];          /* 生效映射副本 */
    mod_motor_channel_state_t state[MOD_MOTOR_MAX]; /* 通道运行态 */
    drv_pwm_ctx_t pwm_ctx[MOD_MOTOR_MAX];           /* PWM 驱动上下文 */
    drv_encoder_ctx_t enc_ctx[MOD_MOTOR_MAX];       /* 编码器驱动对象 */
} mod_motor_ctx_t;

/**
 * @brief 获取默认上下文。
 * @return mod_motor_ctx_t* 默认上下文地址。
 */
mod_motor_ctx_t *mod_motor_get_default_ctx(void);

/**
 * @brief 初始化模块上下文。
 * @param ctx 目标上下文。
 * @param bind 可选绑定参数，传 NULL 表示仅初始化上下文。
 * @return true 初始化成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_motor_ctx_init(mod_motor_ctx_t *ctx, const mod_motor_bind_t *bind);

/**
 * @brief 反初始化模块上下文。
 * @param ctx 目标上下文。
 */
void mod_motor_ctx_deinit(mod_motor_ctx_t *ctx);

/**
 * @brief 绑定硬件映射。
 * @param ctx 目标上下文。
 * @param bind 绑定参数。
 * @return true 绑定成功。
 * @return false 绑定失败。
 */
bool mod_motor_bind(mod_motor_ctx_t *ctx, const mod_motor_bind_t *bind);

/**
 * @brief 解绑硬件映射并清运行态。
 * @param ctx 目标上下文。
 */
void mod_motor_unbind(mod_motor_ctx_t *ctx);

/**
 * @brief 查询上下文是否可运行。
 * @param ctx 目标上下文。
 * @return true 已初始化且已绑定。
 * @return false 未就绪。
 */
bool mod_motor_is_bound(const mod_motor_ctx_t *ctx);

/**
 * @brief 初始化运行通道（PWM/编码器）并进入安全态。
 * @param ctx 目标上下文。
 */
void mod_motor_init(mod_motor_ctx_t *ctx);

/**
 * @brief 设置运行模式。
 * @param ctx 目标上下文。
 * @param id 电机通道。
 * @param mode 目标模式。
 */
void mod_motor_set_mode(mod_motor_ctx_t *ctx, mod_motor_id_e id, mod_motor_mode_e mode);

/**
 * @brief 设置驱动占空比命令（支持正反方向）。
 * @param ctx 目标上下文。
 * @param id 电机通道。
 * @param duty 目标 duty（负值表示反向）。
 */
void mod_motor_set_duty(mod_motor_ctx_t *ctx, mod_motor_id_e id, int16_t duty);

/**
 * @brief 周期更新入口。
 * @param ctx 目标上下文。
 */
void mod_motor_tick(mod_motor_ctx_t *ctx);

/**
 * @brief 获取当前速度反馈。
 * @param ctx 目标上下文。
 * @param id 电机通道。
 * @return int32_t 当前速度；非法参数返回 0。
 */
int32_t mod_motor_get_speed(const mod_motor_ctx_t *ctx, mod_motor_id_e id);

/**
 * @brief 获取累计位置反馈。
 * @param ctx 目标上下文。
 * @param id 电机通道。
 * @return int64_t 累计位置；非法参数返回 0。
 */
int64_t mod_motor_get_position(const mod_motor_ctx_t *ctx, mod_motor_id_e id);

#endif /* FINAL_GRADUATE_WORK_MOD_MOTOR_H */

