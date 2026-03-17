#include "mod_battery.h"

/* 静态全局变量，用于缓存转换后的电压值，防止外部直接修改 */
static float s_battery_voltage = 0.0f; // 缓存当前计算得到的电池电压值

bool mod_battery_update(void)
{
    bool result = true;
    uint16_t raw_avg = 0U;
    float voltage_on_pin = 0.0f;

    /*
     * 1. 调用驱动层接口获取平均原始值
     * 传入 &hadc1 (由 adc.h 提供)，采样 10 次以滤除噪声
     */
    if (drv_adc_read_raw_avg(&hadc1, MOD_BATTERY_CNT, &raw_avg))
    {
        /**
         * 2. 换算逻辑：
         * 第一步：将 0-4095 转换为 ADC 引脚上的实际电压 (0V - 3.3V)
         * 公式：(原始值 / 4095) * 3.3
         */
        voltage_on_pin = ((float)raw_avg / MOD_BATTERY_ADC_RES) * MOD_BATTERY_ADC_REF_V;

        /**
         * 第二步：根据硬件分压电路计算实际电池电压
         * 根据要求：实际电压 = 读取电压 * 10
         */
        s_battery_voltage = voltage_on_pin * MOD_BATTERY_VOL_RATIO;
    }
    else
        result = false;

    return result;
}

float mod_battery_get_voltage(void)
{
    return s_battery_voltage;
}
