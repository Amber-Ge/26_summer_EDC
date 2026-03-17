/**
 ******************************************************************************
 * @file    mod_motor.h
 * @brief   双电机模块接口
 * @details
 * 1. 模块层负责电机业务控制逻辑（模式、占空比、速度位置读数）。
 * 2. 模块不再提供默认硬件回落，必须先显式绑定硬件映射。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_MOTOR_H
#define FINAL_GRADUATE_WORK_MOD_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_encoder.h"
#include "main.h"

/** 电机占空比上限 */
#define MOD_MOTOR_DUTY_MAX (1000U)

/** 电机逻辑通道ID */
typedef enum
{
    MOD_MOTOR_LEFT = 0,
    MOD_MOTOR_RIGHT,
    MOD_MOTOR_MAX
} mod_motor_id_e;

/** 电机运行模式 */
typedef enum
{
    MOTOR_MODE_DRIVE = 0,
    MOTOR_MODE_BRAKE,
    MOTOR_MODE_COAST
} mod_motor_mode_e;

/** 单个电机通道硬件绑定配置 */
typedef struct
{
    GPIO_TypeDef *in1_port;
    uint16_t in1_pin;
    GPIO_TypeDef *in2_port;
    uint16_t in2_pin;

    TIM_HandleTypeDef *pwm_htim;
    uint32_t pwm_channel;
    bool pwm_invert;

    TIM_HandleTypeDef *enc_htim;
    uint8_t enc_counter_bits;
    bool enc_invert;
} mod_motor_hw_cfg_t;

/**
 * @brief 绑定电机硬件映射表
 * @param map     映射表首地址
 * @param map_num 映射项数量，必须等于 MOD_MOTOR_MAX
 * @return true  绑定成功
 * @return false 绑定失败（参数非法）
 */
bool mod_motor_bind_map(const mod_motor_hw_cfg_t *map, uint8_t map_num);

/**
 * @brief 解绑电机硬件映射表
 */
void mod_motor_unbind_map(void);

/**
 * @brief 查询电机模块是否已绑定
 */
bool mod_motor_is_bound(void);

void mod_motor_init(void);
void mod_motor_set_mode(mod_motor_id_e id, mod_motor_mode_e mode);
void mod_motor_set_duty(mod_motor_id_e id, int16_t duty);
void mod_motor_tick(void);
int32_t mod_motor_get_speed(mod_motor_id_e id);
int64_t mod_motor_get_position(mod_motor_id_e id);

#endif /* FINAL_GRADUATE_WORK_MOD_MOTOR_H */