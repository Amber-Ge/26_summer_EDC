#include "mod_battery.h"

#include "drv_adc.h"

/* 电池电压缓存值（单位：V） */
static float s_battery_voltage = 0.0f;
/* 当前绑定的ADC句柄 */
static ADC_HandleTypeDef *s_battery_hadc = NULL;
/* 绑定状态标志：true表示已绑定，false表示未绑定 */
static bool s_battery_bound = false;

bool mod_battery_bind_adc(ADC_HandleTypeDef *hadc)
{
    // 1. 参数校验：句柄为空时直接返回失败。
    if (hadc == NULL)
    {
        return false;
    }

    // 2. 保存ADC句柄并设置绑定状态。
    s_battery_hadc = hadc;
    s_battery_bound = true;

    // 3. 返回绑定成功。
    return true;
}

void mod_battery_unbind_adc(void)
{
    // 1. 清空句柄。
    s_battery_hadc = NULL;

    // 2. 清空绑定状态。
    s_battery_bound = false;
}

bool mod_battery_is_bound(void)
{
    // 1. 只有“状态为已绑定 + 句柄不为空”才判定为已绑定。
    return (bool)(s_battery_bound && (s_battery_hadc != NULL));
}

bool mod_battery_update(void)
{
    bool result = true;
    uint16_t raw_avg = 0U;
    float voltage_on_pin = 0.0f;

    // 1. 强制绑定检查：未绑定时直接失败，不再使用默认ADC回落。
    if (!mod_battery_is_bound())
    {
        return false;
    }

    // 2. 通过驱动层读取ADC平均原始值。
    if (drv_adc_read_raw_avg(s_battery_hadc, MOD_BATTERY_CNT, &raw_avg))
    {
        // 3. 把原始值换算成ADC引脚电压。
        voltage_on_pin = ((float)raw_avg / MOD_BATTERY_ADC_RES) * MOD_BATTERY_ADC_REF_V;

        // 4. 按分压比还原电池真实电压并写入缓存。
        s_battery_voltage = voltage_on_pin * MOD_BATTERY_VOL_RATIO;
    }
    else
    {
        // 5. 采样失败：保留旧缓存并返回失败。
        result = false;
    }

    // 6. 返回本次更新结果。
    return result;
}

float mod_battery_get_voltage(void)
{
    // 1. 返回模块缓存中的最新电压值。
    return s_battery_voltage;
}