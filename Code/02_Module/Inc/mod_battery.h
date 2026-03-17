/**
 ******************************************************************************
 * @file    mod_battery.h
 * @brief   电池电压模块接口定义
 * @details
 * 基于 ADC 原始值完成电压换算，并缓存最近一次有效电压值供上层读取。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_BATTERY_H
#define FINAL_GRADUATE_WORK_MOD_BATTERY_H // 头文件防重复包含宏

#include "drv_adc.h"

/** ADC 参考电压（V） */
#define MOD_BATTERY_ADC_REF_V    (3.3f) // ADC 参考电压，单位 V
/** 12 位 ADC 最大量化值 */
#define MOD_BATTERY_ADC_RES      (4095.0f) // 12位 ADC 满量程计数
/** 电压分压换算比例 */
#define MOD_BATTERY_VOL_RATIO    (10) // 电压分压换算比例
/** 平均采样次数 */
#define MOD_BATTERY_CNT          (10U) // 电压采样平均次数

/**
 * @brief 更新一次电池电压缓存值。
 * @details
 * 函数内部会进行多次 ADC 采样取平均，并执行分压比例换算。
 *
 * @return true 更新成功。
 * @return false 采样失败或换算流程异常。
 */
bool mod_battery_update(void);

/**
 * @brief 获取当前缓存的电池电压值。
 * @return float 电池电压值（单位 V）。
 */
float mod_battery_get_voltage(void);

#endif /* FINAL_GRADUATE_WORK_MOD_BATTERY_H */
