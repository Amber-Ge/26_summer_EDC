/**
 ******************************************************************************
 * @file    mod_battery.h
 * @brief   电池电压模块接口
 * @details
 * 1. 模块层只负责电压业务换算：ADC原始值 -> 实际电池电压。
 * 2. 模块层不再提供默认硬件回落，必须先显式绑定ADC句柄。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_BATTERY_H
#define FINAL_GRADUATE_WORK_MOD_BATTERY_H

#include <stdbool.h>
#include "main.h"

/* ADC参考电压（单位：V） */
#define MOD_BATTERY_ADC_REF_V    (3.3f)
/* 12位ADC满量程 */
#define MOD_BATTERY_ADC_RES      (4095.0f)
/* 电阻分压还原系数 */
#define MOD_BATTERY_VOL_RATIO    (10)
/* 单次更新时的平均采样次数 */
#define MOD_BATTERY_CNT          (10U)

/**
 * @brief 绑定电池模块使用的ADC句柄
 * @param hadc ADC句柄指针
 * @return true  绑定成功
 * @return false 绑定失败（hadc为空）
 */
bool mod_battery_bind_adc(ADC_HandleTypeDef *hadc);

/**
 * @brief 解绑电池模块当前ADC句柄
 */
void mod_battery_unbind_adc(void);

/**
 * @brief 查询电池模块是否已完成ADC绑定
 * @return true 已绑定
 * @return false 未绑定
 */
bool mod_battery_is_bound(void);

/**
 * @brief 更新一次电池电压缓存值
 * @return true  更新成功
 * @return false 更新失败（未绑定或采样失败）
 */
bool mod_battery_update(void);

/**
 * @brief 获取当前缓存的电池电压值（单位：V）
 */
float mod_battery_get_voltage(void);

#endif /* FINAL_GRADUATE_WORK_MOD_BATTERY_H */