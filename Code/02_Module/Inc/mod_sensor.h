/**
 ******************************************************************************
 * @file    mod_sensor.h
 * @brief   循迹传感器模块接口
 * @details
 * 1. 模块层管理12路传感器采样和权重计算。
 * 2. 模块不再提供默认引脚映射，必须先显式绑定映射表。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_SENSOR_H
#define FINAL_GRADUATE_WORK_MOD_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

/** 传感器通道总数 */
#define MOD_SENSOR_CHANNEL_NUM (12U)

/** 单路传感器绑定项 */
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    float factor;
} mod_sensor_map_item_t;

/**
 * @brief 绑定完整传感器映射表
 * @param map     映射表首地址
 * @param map_num 映射项数量，必须等于 MOD_SENSOR_CHANNEL_NUM
 */
bool mod_sensor_bind_map(const mod_sensor_map_item_t *map, uint8_t map_num);

/**
 * @brief 解绑传感器映射表
 */
void mod_sensor_unbind_map(void);

/**
 * @brief 查询传感器模块是否已绑定
 */
bool mod_sensor_is_bound(void);

void mod_sensor_init(void);
uint16_t mod_sensor_get_raw_data(void);
float mod_sensor_get_weight(void);

#endif /* FINAL_GRADUATE_WORK_MOD_SENSOR_H */