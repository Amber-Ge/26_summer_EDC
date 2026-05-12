/**
 * @file    mod_motor.h
 * @brief   Dual motor module public interface.
 */
#ifndef FINAL_GRADUATE_WORK_MOD_MOTOR_H
#define FINAL_GRADUATE_WORK_MOD_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_encoder.h"
#include "drv_gpio.h"
#include "drv_pwm.h"

#define MOD_MOTOR_DUTY_MAX (1000U)

typedef enum
{
    MOD_MOTOR_LEFT = 0,
    MOD_MOTOR_RIGHT,
    MOD_MOTOR_MAX
} mod_motor_id_e;

typedef enum
{
    MOTOR_MODE_DRIVE = 0,
    MOTOR_MODE_BRAKE,
    MOTOR_MODE_COAST
} mod_motor_mode_e;

typedef struct
{
    drv_gpio_pin_t in1;
    drv_gpio_pin_t in2;
    drv_pwm_bind_t pwm;
    drv_encoder_bind_t encoder;
} mod_motor_hw_cfg_t;

typedef struct
{
    const mod_motor_hw_cfg_t *map;
    uint8_t map_num;
} mod_motor_bind_t;

typedef struct
{
    mod_motor_mode_e mode;
    int8_t last_sign;
    uint8_t zero_cross_pending;
    int16_t pending_duty;
    int32_t current_speed;
    int64_t total_position;
    bool pwm_ready;
    bool enc_ready;
} mod_motor_channel_state_t;

typedef struct
{
    bool inited;
    bool bound;
    mod_motor_hw_cfg_t map[MOD_MOTOR_MAX];
    mod_motor_channel_state_t state[MOD_MOTOR_MAX];
    drv_pwm_ctx_t pwm_ctx[MOD_MOTOR_MAX];
    drv_encoder_ctx_t enc_ctx[MOD_MOTOR_MAX];
} mod_motor_ctx_t;

mod_motor_ctx_t *mod_motor_get_default_ctx(void);
bool mod_motor_ctx_init(mod_motor_ctx_t *ctx, const mod_motor_bind_t *bind);
void mod_motor_ctx_deinit(mod_motor_ctx_t *ctx);
bool mod_motor_bind(mod_motor_ctx_t *ctx, const mod_motor_bind_t *bind);
void mod_motor_unbind(mod_motor_ctx_t *ctx);
bool mod_motor_is_bound(const mod_motor_ctx_t *ctx);
void mod_motor_init(mod_motor_ctx_t *ctx);
void mod_motor_set_mode(mod_motor_ctx_t *ctx, mod_motor_id_e id, mod_motor_mode_e mode);
void mod_motor_set_duty(mod_motor_ctx_t *ctx, mod_motor_id_e id, int16_t duty);
void mod_motor_tick(mod_motor_ctx_t *ctx);
int32_t mod_motor_get_speed(const mod_motor_ctx_t *ctx, mod_motor_id_e id);
int64_t mod_motor_get_position(const mod_motor_ctx_t *ctx, mod_motor_id_e id);
bool mod_motor_is_encoder_ready(const mod_motor_ctx_t *ctx, mod_motor_id_e id);
int32_t mod_motor_get_encoder_counter_raw(const mod_motor_ctx_t *ctx, mod_motor_id_e id);

#endif /* FINAL_GRADUATE_WORK_MOD_MOTOR_H */
