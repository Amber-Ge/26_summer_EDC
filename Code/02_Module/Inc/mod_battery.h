/**
 * @file    mod_battery.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   电池电压模块接口（ctx 架构）。
 * @details
 * 1. 模块职责：封装 ADC 原始值采样与“原始值 -> 电池电压”换算。
 * 2. 解耦边界：本模块不负责 ADC 初始化与通道配置，只消费已注入的 ADC 句柄。
 * 3. 生命周期：`ctx_init -> bind -> update/get_voltage -> unbind/deinit`。
 * 4. 兼容策略：仅保留 ctx 接口，不提供旧版全局兼容函数。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_BATTERY_H
#define FINAL_GRADUATE_WORK_MOD_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#include "adc.h"

/* 默认换算参数：3.3V 参考电压，12bit 满量程，电阻分压恢复系数 10，均值采样 10 次。 */
#define MOD_BATTERY_DEFAULT_ADC_REF_V      (3.3f)
#define MOD_BATTERY_DEFAULT_ADC_RES        (4095.0f)
#define MOD_BATTERY_DEFAULT_VOL_RATIO      (10.0f)
#define MOD_BATTERY_DEFAULT_SAMPLE_CNT     (10U)

/**
 * @brief 电池模块绑定参数。
 */
typedef struct
{
    ADC_HandleTypeDef *hadc; // ADC 句柄
    float adc_ref_v;         // ADC 参考电压（V）
    float adc_res;           // ADC 满量程（12bit 为 4095）
    float voltage_ratio;     // 分压还原系数
    uint8_t sample_cnt;      // 单次更新时平均采样次数
} mod_battery_bind_t;

/**
 * @brief 电池模块运行上下文。
 */
typedef struct
{
    bool inited;         // 上下文是否已初始化
    bool bound;          // 是否已绑定参数
    ADC_HandleTypeDef *hadc;
    float adc_ref_v;
    float adc_res;
    float voltage_ratio;
    uint8_t sample_cnt;
    float cached_voltage; // 最近一次更新成功后的缓存电压（V）
} mod_battery_ctx_t;

/**
 * @brief 获取模块默认上下文。
 * @return mod_battery_ctx_t* 默认上下文地址。
 */
mod_battery_ctx_t *mod_battery_get_default_ctx(void);

/**
 * @brief 初始化电池模块上下文，可选直接绑定参数。
 * @param ctx 目标上下文，不可为 NULL。
 * @param bind 可选绑定参数，为 NULL 表示仅初始化。
 * @return true 初始化成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_battery_ctx_init(mod_battery_ctx_t *ctx, const mod_battery_bind_t *bind);

/**
 * @brief 反初始化电池模块上下文。
 * @param ctx 目标上下文。
 */
void mod_battery_ctx_deinit(mod_battery_ctx_t *ctx);

/**
 * @brief 绑定电池采样参数。
 * @param ctx 目标上下文，必须已初始化。
 * @param bind 绑定参数，不可为 NULL。
 * @return true 绑定成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_battery_bind(mod_battery_ctx_t *ctx, const mod_battery_bind_t *bind);

/**
 * @brief 解绑电池采样参数。
 * @param ctx 目标上下文。
 */
void mod_battery_unbind(mod_battery_ctx_t *ctx);

/**
 * @brief 查询上下文是否已可用于运行期采样。
 * @param ctx 目标上下文。
 * @return true 上下文已就绪。
 * @return false 上下文未就绪。
 */
bool mod_battery_is_bound(const mod_battery_ctx_t *ctx);

/**
 * @brief 更新一次电池电压缓存。
 * @param ctx 目标上下文。
 * @return true 更新成功。
 * @return false 未绑定或采样失败。
 */
bool mod_battery_update(mod_battery_ctx_t *ctx);

/**
 * @brief 获取当前缓存电压值（单位：V）。
 * @param ctx 目标上下文。
 * @return float 缓存电压，若 `ctx` 为 NULL 返回 0.0f。
 */
float mod_battery_get_voltage(const mod_battery_ctx_t *ctx);

#endif /* FINAL_GRADUATE_WORK_MOD_BATTERY_H */
