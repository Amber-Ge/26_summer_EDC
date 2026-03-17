/**
 ******************************************************************************
 * @file    mod_sensor.h
 * @brief   传感器模块接口定义
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_SENSOR_H
#define FINAL_GRADUATE_WORK_MOD_SENSOR_H // 头文件防重复包含宏

#include "drv_gpio.h"
#include <stdint.h>

/**
 * @brief 初始化传感器模块。
 */
void mod_sensor_init(void);

/**
 * @brief 读取传感器原始位图数据。
 * @details
 * 返回值的 bit0..bit11 分别对应 S1..S12 的数字状态。
 *
 * @return uint16_t 12 路传感器原始位图。
 */
uint16_t mod_sensor_get_raw_data(void);

/**
 * @brief 读取并计算传感器权重值。
 * @details
 * 基于原始位图计算归一化权重，结果通常限制在 `[-1.0, 1.0]`。
 *
 * @return float 归一化权重值。
 */
float mod_sensor_get_weight(void);

#endif /* FINAL_GRADUATE_WORK_MOD_SENSOR_H */
