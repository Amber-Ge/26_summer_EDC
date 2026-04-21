#ifndef FINAL_GRADUATE_WORK_DRV_ENCODER_H
#define FINAL_GRADUATE_WORK_DRV_ENCODER_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

#define DRV_ENCODER_BITS_16 (16U)
#define DRV_ENCODER_BITS_32 (32U)

typedef enum
{
    DRV_ENCODER_STATUS_OK = 0,
    DRV_ENCODER_STATUS_INVALID_PARAM,
    DRV_ENCODER_STATUS_INVALID_STATE,
    DRV_ENCODER_STATUS_HAL_FAIL
} drv_encoder_status_e;

typedef struct
{
    TIM_HandleTypeDef *instance;
    uint8_t counter_bits;
    bool invert;
} drv_encoder_bind_t;

typedef struct
{
    TIM_HandleTypeDef *htim;
    uint8_t counter_bits;
    int8_t direction;
    int32_t delta_last;
    bool started;
} drv_encoder_ctx_t;

drv_encoder_status_e drv_encoder_ctx_init(drv_encoder_ctx_t *ctx, const drv_encoder_bind_t *bind);
drv_encoder_status_e drv_encoder_start(drv_encoder_ctx_t *ctx);
drv_encoder_status_e drv_encoder_stop(drv_encoder_ctx_t *ctx);
drv_encoder_status_e drv_encoder_reset(drv_encoder_ctx_t *ctx);
drv_encoder_status_e drv_encoder_get_delta(drv_encoder_ctx_t *ctx, int32_t *delta_out);
bool drv_encoder_is_started(const drv_encoder_ctx_t *ctx);

#endif /* FINAL_GRADUATE_WORK_DRV_ENCODER_H */
