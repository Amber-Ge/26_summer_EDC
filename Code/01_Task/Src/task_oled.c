/**
 * @file    task_oled.c
 * @brief   OLED 显示任务实现。
 * @details
 * 1. 文件作用：实现 OLED 页面更新与显示内容调度。
 * 2. 上下层绑定：上层由任务调度层调用；下层依赖 `mod_oled` 显示模块接口。
 */
#include "task_oled.h"
#include "adc.h"
#include "i2c.h"
#include "mod_oled.h"
#include "task_dcc.h"
#include "task_init.h"

/**
 * @brief 判断当前 tick 是否达到目标 tick（支持回绕）。
 * @param now 当前 tick。
 * @param target 目标触发 tick。
 * @return 达到/超过目标返回 1，否则返回 0。
 */
static uint8_t task_oled_time_reached(uint32_t now, uint32_t target)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return (uint8_t)((int32_t)(now - target) >= 0);
}

/**
 * @brief OLED 任务主循环：周期采样电池电压并刷新模式/状态显示。
 * @param argument 任务参数（未使用）。
 * @return 无。
 */
void StartOledTask(void *argument)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint32_t tick_freq; // RTOS tick 频率
    uint32_t oled_period_tick; // OLED 刷新周期（tick）
    uint32_t sample_period_tick; // 电池采样周期（tick）
    uint32_t next_oled_tick; // 下次 OLED 刷新时刻
    uint32_t next_sample_tick; // 下次电池采样时刻
    float latest_voltage = 0.0f; // 最近一次有效电压值
    uint8_t mode_value; // DCC 当前模式
    uint8_t run_state_value; // DCC 当前运行状态

    char voltage_label[] = "voltage:"; // 电压标签字符串
    char mode_label[] = "mode:"; // 模式标签字符串
    char task_on[] = "task:ON"; // 任务开启状态字符串
    char task_off[] = "task:OFF"; // 任务关闭状态字符串
    char task_prepare[] = "task:PREP"; // 任务准备状态字符串
    char task_stop[] = "task:STOP"; // 任务停止状态字符串

    (void)argument;

    task_wait_init_done();

    (void)OLED_BindI2C(&hi2c2, OLED_I2C_ADDR_DEFAULT, OLED_I2C_TIMEOUT_MS_DEFAULT);
    (void)mod_battery_bind_adc(&hadc1);

    tick_freq = osKernelGetTickFreq();
    if (tick_freq == 0U)
    {
        tick_freq = 1000U;
    }

    oled_period_tick = (uint32_t)(((uint64_t)TASK_OLED_REFRESH_MS * (uint64_t)tick_freq + 999ULL) / 1000ULL);
    sample_period_tick = TASK_OLED_SAMPLE_S * tick_freq;

    if (oled_period_tick == 0U)
    {
        oled_period_tick = 1U;
    }
    if (sample_period_tick == 0U)
    {
        sample_period_tick = 1U;
    }

    OLED_Init();
    if (mod_battery_update())
    {
        latest_voltage = mod_battery_get_voltage();
    }

    next_oled_tick = osKernelGetTickCount();
    next_sample_tick = next_oled_tick + sample_period_tick;

    for (;;) // 循环计数器
    {
        uint32_t now_tick = osKernelGetTickCount(); // 当前系统 tick

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

        if (task_oled_time_reached(now_tick, next_oled_tick))
        {
            mode_value = task_dcc_get_mode();
            run_state_value = task_dcc_get_run_state();

            OLED_Clear();
            OLED_ShowString(0U, 0U, voltage_label, OLED_8X16);
            OLED_ShowFloatNum(0U, 16U, latest_voltage, 2U, 2U, OLED_8X16);
            OLED_ShowString(0U, 32U, mode_label, OLED_8X16);
            OLED_ShowNum(48U, 32U, (uint32_t)mode_value, 1U, OLED_8X16);

            if (run_state_value == TASK_DCC_RUN_ON)
            {
                OLED_ShowString(0U, 48U, task_on, OLED_8X16);
            }
            else if (run_state_value == TASK_DCC_RUN_STOP)
            {
                OLED_ShowString(0U, 48U, task_stop, OLED_8X16);
            }
            else if (run_state_value == TASK_DCC_RUN_PREPARE)
            {
                OLED_ShowString(0U, 48U, task_prepare, OLED_8X16);
            }
            else
            {
                OLED_ShowString(0U, 48U, task_off, OLED_8X16);
            }

            OLED_Update();

            next_oled_tick += oled_period_tick;
            while (task_oled_time_reached(now_tick, next_oled_tick))
            {
                next_oled_tick += oled_period_tick;
            }
        }

        osDelay(TASK_OLED_IDLE_DELAY_TICK);
    }
}



