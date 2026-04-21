/**
* @file    drv_adc.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   ADC 驱动接口声明。
 * @details
 * 1. 本驱动提供 ADC 原始值读取能力：单次读取与多次平均读取。
 * 2. 本驱动只返回原始计数值，不负责电压换算与业务阈值判定。
 * 3. ADC 初始化、校准与通道配置由 Core/HAL 初始化流程负责。
 */
#ifndef FINAL_GRADUATE_WORK_DRV_ADC_H
#define FINAL_GRADUATE_WORK_DRV_ADC_H

#include <stdbool.h>
#include <stdint.h>

#include "adc.h"
#include "main.h"

/** ADC 阻塞轮询超时时间（ms）。 */
#define DRV_ADC_TIMEOUT_MS    (10U)

/**
 * @brief 读取一次 ADC 原始值。
 * @param hadc ADC 硬件句柄。
 * @param out_raw 输出参数，返回原始采样值（0~4095）。
 * @return true 读取成功。
 * @return false 参数非法或 HAL 转换失败。
 */
bool drv_adc_read_raw(ADC_HandleTypeDef *hadc, uint16_t *out_raw);

/**
 * @brief 多次采样并返回 ADC 原始平均值。
 * @param hadc ADC 硬件句柄。
 * @param sample_cnt 采样次数，必须大于 0。
 * @param out_raw_avg 输出参数，返回平均值。
 * @return true 全部采样成功。
 * @return false 参数非法或任一采样失败。
 */
bool drv_adc_read_raw_avg(ADC_HandleTypeDef *hadc, uint8_t sample_cnt, uint16_t *out_raw_avg);

#endif /* FINAL_GRADUATE_WORK_DRV_ADC_H */
