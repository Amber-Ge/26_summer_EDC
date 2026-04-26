/**
 * @file    mod_vision.h
 * @author  Amber Ge
 * @brief   统一视觉语义模块接口。
 * @details
 * 1. 模块职责：对不同视觉设备的输出做统一语义翻译，并维护最新视觉状态。
 * 2. 模块边界：不处理 UART/DMA/具体协议收发，不承担云台/小车项目控制逻辑。
 * 3. 当前来源：先接入 K230；后续可继续扩展其他视觉模块输入。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_VISION_H
#define FINAL_GRADUATE_WORK_MOD_VISION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    MOD_VISION_SOURCE_NONE = 0,
    MOD_VISION_SOURCE_K230,
    MOD_VISION_SOURCE_MAX
} mod_vision_source_t;

typedef struct
{
    bool valid;
    mod_vision_source_t source;
    uint32_t update_seq;
    uint32_t update_tick;
    uint8_t x_target_id;
    int16_t x_error;
    uint8_t y_target_id;
    int16_t y_error;
} mod_vision_data_t;

typedef struct
{
    bool inited;
    mod_vision_data_t latest;
} mod_vision_ctx_t;

mod_vision_ctx_t *mod_vision_get_default_ctx(void);
bool mod_vision_ctx_init(mod_vision_ctx_t *ctx);
void mod_vision_ctx_deinit(mod_vision_ctx_t *ctx);
void mod_vision_clear(mod_vision_ctx_t *ctx);

bool mod_vision_get_latest_data(const mod_vision_ctx_t *ctx, mod_vision_data_t *out_data);
bool mod_vision_has_valid_data(const mod_vision_ctx_t *ctx);
bool mod_vision_is_data_stale(const mod_vision_ctx_t *ctx, uint32_t timeout_ms);

bool mod_vision_update_from_k230(mod_vision_ctx_t *ctx,
                                 uint8_t x_target_id,
                                 int16_t x_error,
                                 uint8_t y_target_id,
                                 int16_t y_error);

#endif /* FINAL_GRADUATE_WORK_MOD_VISION_H */
