/**
 * @file    mod_sensor.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   循迹传感器模块接口（ctx 架构）。
 * @details
 * 1. 文件作用：提供多路循迹通道映射绑定、状态采样和权重计算能力。
 * 2. 解耦边界：模块层只负责采样语义与权重归一化，不承担任务调度和控制决策。
 * 3. 上层调用：Task 层通过 `ctx` 调用 `get_states/get_weight` 获取当前采样结果。
 * 4. 下层依赖：通过 `drv_gpio` 读取输入电平。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_SENSOR_H
#define FINAL_GRADUATE_WORK_MOD_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"
#include "main.h"

#define MOD_SENSOR_CHANNEL_NUM (12U)

/**
 * @brief 单路传感器硬件映射配置。
 */
typedef struct
{
    GPIO_TypeDef *port;      // GPIO 端口句柄
    uint16_t pin;            // GPIO 引脚掩码
    gpio_level_e line_level; // 判定为“检测到黑线”时的有效电平
    float factor;            // 权重系数，用于偏差计算
} mod_sensor_map_item_t;

/**
 * @brief 传感器绑定输入参数。
 */
typedef struct
{
    const mod_sensor_map_item_t *map; // 映射表首地址
    uint8_t map_num;                  // 映射项数量，必须等于 MOD_SENSOR_CHANNEL_NUM
} mod_sensor_bind_t;

/**
 * @brief 传感器模块运行上下文。
 */
typedef struct
{
    bool inited;                                       // 上下文是否已初始化
    bool bound;                                        // 上下文是否已完成绑定
    mod_sensor_map_item_t map[MOD_SENSOR_CHANNEL_NUM]; // 当前生效映射表副本
    uint8_t states[MOD_SENSOR_CHANNEL_NUM];            // 最近一次采样状态缓存（黑线=1）
    float weight;                                      // 最近一次归一化权重结果
} mod_sensor_ctx_t;

/**
 * @brief 获取模块默认上下文。
 * @return mod_sensor_ctx_t* 默认上下文地址。
 */
mod_sensor_ctx_t *mod_sensor_get_default_ctx(void);

/**
 * @brief 初始化传感器上下文，可选直接绑定映射。
 * @param ctx 目标上下文。
 * @param bind 可选绑定参数；传 NULL 表示仅初始化。
 * @return true 初始化成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_sensor_ctx_init(mod_sensor_ctx_t *ctx, const mod_sensor_bind_t *bind);

/**
 * @brief 反初始化传感器上下文。
 * @param ctx 目标上下文。
 */
void mod_sensor_ctx_deinit(mod_sensor_ctx_t *ctx);

/**
 * @brief 绑定传感器映射表。
 * @param ctx 目标上下文。
 * @param bind 绑定参数。
 * @return true 绑定成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_sensor_bind(mod_sensor_ctx_t *ctx, const mod_sensor_bind_t *bind);

/**
 * @brief 解绑传感器映射。
 * @param ctx 目标上下文。
 */
void mod_sensor_unbind(mod_sensor_ctx_t *ctx);

/**
 * @brief 查询上下文是否已就绪。
 * @param ctx 目标上下文。
 * @return true 已就绪。
 * @return false 未就绪。
 */
bool mod_sensor_is_bound(const mod_sensor_ctx_t *ctx);

/**
 * @brief 初始化运行缓存。
 * @param ctx 目标上下文。
 */
void mod_sensor_init(mod_sensor_ctx_t *ctx);

/**
 * @brief 读取多路传感器状态（黑线=1，非黑线=0）。
 * @param ctx 目标上下文。
 * @param states 输出数组，长度至少为 MOD_SENSOR_CHANNEL_NUM。
 * @param states_num 输出数组长度。
 * @return true 读取成功。
 * @return false 参数非法或上下文未就绪。
 */
bool mod_sensor_get_states(mod_sensor_ctx_t *ctx, uint8_t *states, uint8_t states_num);

/**
 * @brief 读取归一化权重结果（范围 [-1, 1]）。
 * @param ctx 目标上下文。
 * @return float 权重结果；上下文非法时返回 0。
 */
float mod_sensor_get_weight(mod_sensor_ctx_t *ctx);

#endif /* FINAL_GRADUATE_WORK_MOD_SENSOR_H */
