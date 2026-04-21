#ifndef FINAL_GRADUATE_WORK_DRV_PWM_H
#define FINAL_GRADUATE_WORK_DRV_PWM_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    DRV_PWM_STATUS_OK = 0,
    DRV_PWM_STATUS_INVALID_PARAM,
    DRV_PWM_STATUS_INVALID_STATE,
    DRV_PWM_STATUS_HAL_FAIL
} drv_pwm_status_e;

typedef struct
{
    TIM_HandleTypeDef *instance;
    uint32_t channel;
    uint16_t duty_max;
    bool invert;
} drv_pwm_bind_t;

typedef struct
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
    uint16_t duty_max;
    uint16_t duty_last;
    bool invert;
    bool started;
} drv_pwm_ctx_t;

drv_pwm_status_e drv_pwm_ctx_init(drv_pwm_ctx_t *ctx, const drv_pwm_bind_t *bind);
drv_pwm_status_e drv_pwm_start(drv_pwm_ctx_t *ctx);
drv_pwm_status_e drv_pwm_stop(drv_pwm_ctx_t *ctx);
drv_pwm_status_e drv_pwm_set_duty(drv_pwm_ctx_t *ctx, uint16_t duty);
uint16_t drv_pwm_get_duty_max(const drv_pwm_ctx_t *ctx);
bool drv_pwm_is_started(const drv_pwm_ctx_t *ctx);

#endif /* FINAL_GRADUATE_WORK_DRV_PWM_H */
