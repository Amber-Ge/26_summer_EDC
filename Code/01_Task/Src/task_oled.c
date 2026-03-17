#include "task_oled.h"
#include "mod_oled.h"
#include "adc.h"
#include "i2c.h"

/**
 * @brief  内部时间比对函数：判断目标时间戳是否到达
 * @param  now:    当前系统 Tick 时间
 * @param  target: 预设的目标 Tick 时间
 * @return 1: 时间已到达或过期, 0: 时间未到
 * @note   利用 int32_t 强制转换解决 uint32_t 的 49 天溢出翻转问题
 */
static uint8_t task_oled_time_reached(uint32_t now, uint32_t target)
{
    return (uint8_t)((int32_t)(now - target) >= 0);
}

void StartOledTask(void *argument)
{
    /* --- 变量定义：时间计算相关 --- */
    uint32_t tick_freq;          // 系统内核 Tick 频率
    uint32_t oled_period_tick;   // OLED刷新周期（Tick）
    uint32_t sample_period_tick; // 电池采样周期（Tick）
    uint32_t next_oled_tick;     // 下一次刷屏时刻
    uint32_t next_sample_tick;   // 下一次采样时刻

    /* --- 变量定义：数据处理相关 --- */
    float latest_voltage = 0.0f;       // 最近一次有效电池电压
    char voltage_label[] = "voltage:"; // 屏幕固定标签

    (void)argument; // 显式声明参数未使用，避免编译器警告

    /* 0. 强制显式绑定：无默认回落，必须先绑定才能工作 */
    (void)OLED_BindI2C(&hi2c2, OLED_I2C_ADDR_DEFAULT, OLED_I2C_TIMEOUT_MS_DEFAULT);
    (void)mod_battery_bind_adc(&hadc1);

    /* 1. 获取系统频率并计算周期对应Tick数 */
    tick_freq = osKernelGetTickFreq();
    if (tick_freq == 0U)
    {
        tick_freq = 1000U;
    }

    oled_period_tick = TASK_OLED_REFRESH_S * tick_freq;
    sample_period_tick = TASK_OLED_SAMPLE_S * tick_freq;

    /* 2. 防御性处理：周期至少为1Tick */
    if (oled_period_tick == 0U)
    {
        oled_period_tick = 1U;
    }
    if (sample_period_tick == 0U)
    {
        sample_period_tick = 1U;
    }

    /* 3. 初始化OLED并更新一次电池缓存 */
    OLED_Init();
    if (mod_battery_update())
    {
        latest_voltage = mod_battery_get_voltage();
    }

    /* 4. 初始化调度时间轴 */
    next_oled_tick = osKernelGetTickCount();
    next_sample_tick = next_oled_tick + sample_period_tick;

    /* 5. 主循环 */
    for (;;)
    {
        uint32_t now_tick = osKernelGetTickCount();

        /* 5.1 周期采样电池电压 */
        if (task_oled_time_reached(now_tick, next_sample_tick))
        {
            if (mod_battery_update())
            {
                latest_voltage = mod_battery_get_voltage();
            }

            next_sample_tick += sample_period_tick;
            while (task_oled_time_reached(now_tick, next_sample_tick))
            {
                next_sample_tick += sample_period_tick;
            }
        }

        /* 5.2 周期刷新OLED显示 */
        if (task_oled_time_reached(now_tick, next_oled_tick))
        {
            OLED_Clear();
            OLED_ShowString(0U, 0U, voltage_label, OLED_8X16);
            OLED_ShowFloatNum(0U, 16U, latest_voltage, 2U, 2U, OLED_8X16);
            OLED_Update();

            next_oled_tick += oled_period_tick;
            while (task_oled_time_reached(now_tick, next_oled_tick))
            {
                next_oled_tick += oled_period_tick;
            }
        }

        /* 5.3 让出CPU */
        osDelay(TASK_OLED_IDLE_DELAY_TICK);
    }
}