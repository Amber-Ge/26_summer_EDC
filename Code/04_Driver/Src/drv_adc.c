/**
 * @file    drv_adc.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   ADC 驱动接口实现。
 * @details
 * 1. 单次读取流程：Start -> PollForConversion -> GetValue -> Stop。
 * 2. 平均读取流程：重复单次读取并求平均。
 * 3. 解耦边界：仅提供 ADC 原始值采集能力，不做电压换算与业务判定。
 */

#include "drv_adc.h"

/**
 * @brief 读取一次 ADC 原始值。
 * @param hadc ADC 硬件句柄。
 * @param out_raw 输出参数，返回原始采样值（0~4095）。
 * @return true 读取成功。
 * @return false 参数非法或 HAL 转换失败。
 */
bool drv_adc_read_raw(ADC_HandleTypeDef *hadc, uint16_t *out_raw)
{
    HAL_StatusTypeDef hal_ret = HAL_ERROR; // HAL 调用返回码。

    // 步骤1：参数校验。
    if ((hadc == NULL) || (out_raw == NULL))
    {
        return false;
    }

    // 步骤2：启动 ADC 转换。
    hal_ret = HAL_ADC_Start(hadc);
    if (hal_ret != HAL_OK)
    {
        return false;
    }

    // 步骤3：阻塞等待一次转换完成。
    hal_ret = HAL_ADC_PollForConversion(hadc, DRV_ADC_TIMEOUT_MS);
    if (hal_ret != HAL_OK)
    {
        // 失败路径也尝试停止 ADC，避免状态残留。
        (void)HAL_ADC_Stop(hadc);
        return false;
    }

    // 步骤4：读取转换结果并停止 ADC。
    *out_raw = (uint16_t)HAL_ADC_GetValue(hadc);
    (void)HAL_ADC_Stop(hadc);

    // 步骤5：返回成功。
    return true;
}

/**
 * @brief 多次采样并返回 ADC 原始平均值。
 * @param hadc ADC 硬件句柄。
 * @param sample_cnt 采样次数，必须大于 0。
 * @param out_raw_avg 输出参数，返回平均值。
 * @return true 全部采样成功。
 * @return false 参数非法或任一采样失败。
 */
bool drv_adc_read_raw_avg(ADC_HandleTypeDef *hadc, uint8_t sample_cnt, uint16_t *out_raw_avg)
{
    uint32_t sum = 0U; // sum：累加原始值，使用 32 位避免溢出。
    uint16_t raw_temp = 0U; // raw_temp：单次采样临时缓存。

    // 步骤1：参数校验。
    if ((hadc == NULL) || (out_raw_avg == NULL) || (sample_cnt == 0U))
    {
        return false;
    }

    // 步骤2：循环采样并累加。
    for (uint8_t i = 0U; i < sample_cnt; i++)
    {
        if (!drv_adc_read_raw(hadc, &raw_temp))
        {
            return false;
        }

        sum += (uint32_t)raw_temp;
    }

    // 步骤3：计算平均值并输出。
    *out_raw_avg = (uint16_t)(sum / (uint32_t)sample_cnt);

    // 步骤4：返回成功。
    return true;
}
