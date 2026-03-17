#include "task_oled.h"
#include "task_init.h"
#include "mod_oled.h"
#include "adc.h"
#include "i2c.h"

/**
 * @brief 时间到达判断（支持 tick 回绕）。
 *
 * @details
 * 通过 (int32_t)(now - target) 的符号判断“是否到达/超时”，
 * 可以正确处理 uint32_t 计数器回绕场景。
 */
static uint8_t task_oled_time_reached(uint32_t now, uint32_t target)
{
    return (uint8_t)((int32_t)(now - target) >= 0);
}

/**
 * @brief OLED 显示任务入口。
 *
 * @details
 * 注意：按你的要求，OLED 初始化仍保留在本任务中（未迁移到 InitTask）。
 * 本任务仅增加了“等待 InitTask 完成”门控，保证系统启动顺序一致。
 *
 * 运行逻辑：
 * 1. 绑定 OLED I2C 与电池 ADC。
 * 2. 初始化 OLED，并做一次电池采样缓存。
 * 3. 周期采样电池电压 + 周期刷新显示。
 */
void StartOledTask(void *argument)
{
    uint32_t tick_freq;
    uint32_t oled_period_tick;
    uint32_t sample_period_tick;
    uint32_t next_oled_tick;
    uint32_t next_sample_tick;
    float latest_voltage = 0.0f;
    char voltage_label[] = "voltage:";

    (void)argument;

    /* 先走统一门控，再执行 OLED 自身初始化流程。 */
    task_wait_init_done();

    /* 显式绑定通信与采样资源（无默认回落）。 */
    (void)OLED_BindI2C(&hi2c2, OLED_I2C_ADDR_DEFAULT, OLED_I2C_TIMEOUT_MS_DEFAULT);
    (void)mod_battery_bind_adc(&hadc1);

    /* 读取 RTOS tick 频率，防御式处理异常值。 */
    tick_freq = osKernelGetTickFreq();
    if (tick_freq == 0U)
    {
        tick_freq = 1000U;
    }

    oled_period_tick = TASK_OLED_REFRESH_S * tick_freq;
    sample_period_tick = TASK_OLED_SAMPLE_S * tick_freq;

    /* 周期最小保护：至少 1 tick，防止 0 周期导致死循环。 */
    if (oled_period_tick == 0U)
    {
        oled_period_tick = 1U;
    }
    if (sample_period_tick == 0U)
    {
        sample_period_tick = 1U;
    }

    /* 初始化 OLED，并做一次电池采样建立初始显示缓存。 */
    OLED_Init();
    if (mod_battery_update())
    {
        latest_voltage = mod_battery_get_voltage();
    }

    /* 初始化两个调度时间轴：显示轴 + 采样轴。 */
    next_oled_tick = osKernelGetTickCount();
    next_sample_tick = next_oled_tick + sample_period_tick;

    for (;;)
    {
        uint32_t now_tick = osKernelGetTickCount();

        /* 到采样时刻则更新电压缓存。 */
        if (task_oled_time_reached(now_tick, next_sample_tick))
        {
            if (mod_battery_update())
            {
                latest_voltage = mod_battery_get_voltage();
            }

            /* 若任务被抢占导致落后多个周期，则补齐时间轴。 */
            next_sample_tick += sample_period_tick;
            while (task_oled_time_reached(now_tick, next_sample_tick))
            {
                next_sample_tick += sample_period_tick;
            }
        }

        /* 到刷新时刻则重绘屏幕。 */
        if (task_oled_time_reached(now_tick, next_oled_tick))
        {
            OLED_Clear();
            OLED_ShowString(0U, 0U, voltage_label, OLED_8X16);
            OLED_ShowFloatNum(0U, 16U, latest_voltage, 2U, 2U, OLED_8X16);
            OLED_Update();

            /* 同样处理“落后多个刷新周期”的补偿。 */
            next_oled_tick += oled_period_tick;
            while (task_oled_time_reached(now_tick, next_oled_tick))
            {
                next_oled_tick += oled_period_tick;
            }
        }

        /* 主动让出 CPU。 */
        osDelay(TASK_OLED_IDLE_DELAY_TICK);
    }
}
