/**
 * @file    drv_pwm.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   PWM 驱动层接口定义（ctx 架构，无兼容接口）。
 * @details
 * 1. 本驱动用于封装 TIM PWM 通道生命周期：`ctx_init -> start -> set_duty -> stop`。
 * 2. 驱动层仅处理通道启停、比较寄存器写入和参数校验，不承载方向/闭环业务语义。
 * 3. 上层模块（如 mod_motor）通过 ctx 对象持有一个具体 PWM 输出实例。
 */
#ifndef FINAL_GRADUATE_WORK_DRV_PWM_H
#define FINAL_GRADUATE_WORK_DRV_PWM_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief PWM 驱动状态码。
 */
typedef enum
{
    DRV_PWM_STATUS_OK = 0,        /* 调用成功 */
    DRV_PWM_STATUS_INVALID_PARAM, /* 输入参数非法 */
    DRV_PWM_STATUS_INVALID_STATE, /* 对象状态非法（未初始化/未启动） */
    DRV_PWM_STATUS_HAL_FAIL       /* HAL 调用失败 */
} drv_pwm_status_e;

/**
 * @brief PWM 通道上下文。
 * @details
 * 每个 ctx 对应一个 TIM 的一个 PWM 通道，支持独立启停和占空比控制。
 */
typedef struct
{
    TIM_HandleTypeDef *htim; /* 目标定时器句柄 */
    uint32_t channel;        /* TIM 通道号 */
    uint16_t duty_max;       /* 占空比上限（比较寄存器标称最大值） */
    uint16_t duty_last;      /* 最近一次写入后的占空比值（包含反相换算结果） */
    bool invert;             /* 占空比反相标志：true 时写入值为 duty_max - duty */
    bool started;            /* PWM 启动标记 */
} drv_pwm_ctx_t;

/**
 * @brief 初始化 PWM ctx。
 * @param ctx 目标上下文对象。
 * @param htim TIM 句柄。
 * @param channel TIM 通道号（仅支持 TIM_CHANNEL_1~4）。
 * @param duty_max 占空比上限，必须大于 0。
 * @param invert 占空比反相开关。
 * @return drv_pwm_status_e 状态码。
 */
drv_pwm_status_e drv_pwm_ctx_init(drv_pwm_ctx_t *ctx,
                                  TIM_HandleTypeDef *htim,
                                  uint32_t channel,
                                  uint16_t duty_max,
                                  bool invert);

/**
 * @brief 启动 PWM 输出。
 * @param ctx PWM 上下文对象。
 * @return drv_pwm_status_e 状态码。
 */
drv_pwm_status_e drv_pwm_start(drv_pwm_ctx_t *ctx);

/**
 * @brief 停止 PWM 输出。
 * @param ctx PWM 上下文对象。
 * @return drv_pwm_status_e 状态码。
 */
drv_pwm_status_e drv_pwm_stop(drv_pwm_ctx_t *ctx);

/**
 * @brief 设置 PWM 占空比。
 * @details
 * 仅允许在 `started=true` 时调用；输入会先限幅，再按 `invert` 规则换算。
 * @param ctx PWM 上下文对象。
 * @param duty 目标占空比（0~duty_max，越界会限幅）。
 * @return drv_pwm_status_e 状态码。
 */
drv_pwm_status_e drv_pwm_set_duty(drv_pwm_ctx_t *ctx, uint16_t duty);

/**
 * @brief 获取占空比上限配置值。
 * @param ctx PWM 上下文对象。
 * @return uint16_t 上限值；ctx 非法时返回 0。
 */
uint16_t drv_pwm_get_duty_max(const drv_pwm_ctx_t *ctx);

/**
 * @brief 查询 PWM 是否处于已启动状态。
 * @param ctx PWM 上下文对象。
 * @return true 已启动。
 * @return false 未启动或上下文非法。
 */
bool drv_pwm_is_started(const drv_pwm_ctx_t *ctx);

#endif /* FINAL_GRADUATE_WORK_DRV_PWM_H */
