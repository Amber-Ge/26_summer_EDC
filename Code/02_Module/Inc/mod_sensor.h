/**
 ******************************************************************************
 * @file    mod_sensor.h
 * @brief   循迹传感器模块接口
 * @details
 * 1. 模块固定管理 12 路循迹传感器；
 * 2. 统一语义：检测到黑线返回 1，未检测到返回 0；
 * 3. 每一路可独立配置“黑线有效电平”。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_SENSOR_H
#define FINAL_GRADUATE_WORK_MOD_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"
#include "main.h"

#define MOD_SENSOR_CHANNEL_NUM (12U)

typedef struct
{
    GPIO_TypeDef *port;       // GPIO 端口
    uint16_t pin;             // GPIO 引脚
    gpio_level_e line_level;  // 该路“检测到黑线”时的有效电平
    float factor;             // 该路权重系数（用于 weight 计算）
} mod_sensor_map_item_t;

/**
 * @brief 绑定完整传感器映射表（必须为 12 路）
 * @param map 传感器映射表
 * @param map_num 映射项数量，需等于 MOD_SENSOR_CHANNEL_NUM
 * @return true 绑定成功
 * @return false 参数错误或映射非法
 */
bool mod_sensor_bind_map(const mod_sensor_map_item_t *map, uint8_t map_num);

/**
 * @brief 解除传感器映射绑定
 */
void mod_sensor_unbind_map(void);

/**
 * @brief 查询传感器模块是否已经绑定映射
 * @return true 已绑定
 * @return false 未绑定
 */
bool mod_sensor_is_bound(void);

/**
 * @brief 初始化传感器运行缓存
 */
void mod_sensor_init(void);

/**
 * @brief 读取 12 路传感器状态
 * @details 输出语义固定为：黑线=1，非黑线=0
 * @param states 输出数组，长度至少为 MOD_SENSOR_CHANNEL_NUM
 * @param states_num 输出数组长度
 * @return true 读取成功（且模块已绑定）
 * @return false 参数错误或模块未绑定
 */
bool mod_sensor_get_states(uint8_t *states, uint8_t states_num);

/**
 * @brief 读取加权结果（范围限制在 [-1, 1]）
 * @return float 归一化权重结果
 */
float mod_sensor_get_weight(void);

#endif /* FINAL_GRADUATE_WORK_MOD_SENSOR_H */
