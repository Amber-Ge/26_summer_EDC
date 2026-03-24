/**
 * @file    drv_adc.c
 * @brief   ADC 驱动层接口实现。
 * @details
 * 1. 文件作用：实现 ADC 原始值阻塞读取与多次采样平均。
 * 2. 解耦边界：仅输出原始采样结果，不承担电压标定与业务滤波策略。
 * 3. 上层绑定：`mod_battery` 等模块基于原始值执行换算和阈值判断。
 * 4. 下层依赖：调用 HAL ADC 接口访问外设寄存器与采样流程。
 */

#include "drv_adc.h"

/**
 * @brief 读取一次 ADC 原始值。
 * @param hadc ADC 硬件句柄指针。
 * @param out_raw 输出参数，用于返回原始值。
 * @return true 读取成功。
 * @return false 参数无效或转换失败/超时。
 */
bool drv_adc_read_raw(ADC_HandleTypeDef *hadc, uint16_t *out_raw)
{
    bool result = true;            // 函数执行结果标志，默认成功
    HAL_StatusTypeDef hal_ret;     // HAL 接口返回状态

    // 1. 参数有效性检查：句柄和输出地址必须有效。
    if ((hadc == NULL) || (out_raw == NULL))
    {
        result = false;
    }
    else
    {
        // 2. 启动 ADC 单次转换。
        hal_ret = HAL_ADC_Start(hadc);
        if (hal_ret != HAL_OK)
        {
            result = false;
        }
        else
        {
            // 3. 阻塞等待转换完成（带超时保护）。
            hal_ret = HAL_ADC_PollForConversion(hadc, DRV_ADC_TIMEOUT_MS);
            if (hal_ret != HAL_OK)
            {
                result = false;
            }
            else
            {
                // 4. 读取 ADC 原始值。
                *out_raw = (uint16_t)HAL_ADC_GetValue(hadc);
            }

            // 5. 无论成功失败都停止 ADC，避免外设状态残留。
            (void)HAL_ADC_Stop(hadc);
        }
    }

    // 6. 返回最终执行结果。
    return result;
}

/**
 * @brief 多次采样并返回 ADC 原始平均值。
 * @param hadc ADC 硬件句柄指针。
 * @param sample_cnt 采样次数。
 * @param out_raw_avg 输出参数，用于返回平均值。
 * @return true 全部采样成功并输出平均值。
 * @return false 参数无效或任意一次采样失败。
 */
bool drv_adc_read_raw_avg(ADC_HandleTypeDef *hadc, uint8_t sample_cnt, uint16_t *out_raw_avg)
{
    bool result = true;        // 批量采样执行结果标志
    uint32_t sum = 0U;         // 采样累加和，使用 32 位避免溢出
    uint16_t raw_temp = 0U;    // 单次采样临时值

    // 1. 参数有效性检查。
    if ((hadc == NULL) || (out_raw_avg == NULL) || (sample_cnt == 0U))
    {
        result = false;
    }
    else
    {
        // 2. 按采样次数循环读取并累加。
        for (uint8_t i = 0U; i < sample_cnt; i++) // 采样循环计数器
        {
            if (drv_adc_read_raw(hadc, &raw_temp))
            {
                sum += (uint32_t)raw_temp;
            }
            else
            {
                // 3. 任意一次采样失败，整体平均结果判定为失败。
                result = false;
                break;
            }
        }

        // 4. 全部采样成功后输出平均值。
        if (result == true)
        {
            *out_raw_avg = (uint16_t)(sum / (uint32_t)sample_cnt);
        }
    }

    // 5. 返回最终结果。
    return result;
}
