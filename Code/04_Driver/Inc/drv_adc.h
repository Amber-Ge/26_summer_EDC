/**
 * @file    drv_adc.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   ADC 驱动层接口定义。
 * @details
 * 1. 文件作用：提供 ADC 原始采样访问能力（单次读取/平均读取）。
 * 2. 解耦边界：驱动层只返回原始计数值，不承担电压/物理量业务换算。
 * 3. 上层绑定：`mod_battery` 等模块基于该原始值执行业务换算和滤波策略。
 * 4. 下层依赖：直接调用 HAL ADC 接口，ADC 句柄由上层初始化后传入。
 * 5. 生命周期：调用前需确保对应 ADC 外设已完成 `MX_ADCx_Init` 和校准流程。
 */
#ifndef FINAL_GRADUATE_WORK_DRV_ADC_H
#define FINAL_GRADUATE_WORK_DRV_ADC_H

#include "main.h"
#include "adc.h"
#include <stdint.h>
#include <stdbool.h>

/** ADC 阻塞轮询超时时间，单位 ms */
#define DRV_ADC_TIMEOUT_MS    (10U)

/**
 * @brief 读取一次 ADC 原始值。
 * @details
 * 该函数以阻塞轮询方式执行一次完整转换，并返回 12 位原始结果。
 * @param hadc ADC 硬件句柄指针（如 `&hadc1`）。
 * @param out_raw 输出参数，用于返回采样原始值（范围 0~4095）。
 * @return true 读取成功。
 * @return false 参数无效或转换失败/超时。
 */
bool drv_adc_read_raw(ADC_HandleTypeDef *hadc, uint16_t *out_raw);

/**
 * @brief 多次采样并返回 ADC 原始平均值。
 * @details
 * 该函数内部重复调用 `drv_adc_read_raw`，对采样结果求平均，
 * 用于降低随机噪声影响。
 * @param hadc ADC 硬件句柄指针。
 * @param sample_cnt 采样次数，必须大于 0。
 * @param out_raw_avg 输出参数，用于返回平均后的原始值。
 * @return true 全部采样成功并完成平均计算。
 * @return false 参数无效或任意一次采样失败。
 */
bool drv_adc_read_raw_avg(ADC_HandleTypeDef *hadc, uint8_t sample_cnt, uint16_t *out_raw_avg);

#endif /* FINAL_GRADUATE_WORK_DRV_ADC_H */
