/**
 * @file    task_oled.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   OLED 显示任务实现。
 * @details
 * 1. 周期采样电池电压并刷新 OLED 页面显示。
 * 2. 页面包含：电压、电控模式值、DCC 运行状态文本。
 * 3. 任务通过双时基（采样周期 + 刷新周期）降低无效 I2C 访问。
 */

#include "task_oled.h"

#include "i2c.h"
#include "mod_oled.h"
#include "task_dcc.h"
#include "task_init.h"

/**
 * @brief 判断当前 tick 是否达到目标 tick（支持回绕）。
 */
static uint8_t task_oled_time_reached(uint32_t now, uint32_t target)
{
    return (uint8_t)((int32_t)(now - target) >= 0);
}

/**
 * @brief OLED 任务主循环。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartOledTask(void *argument)
{
    uint32_t tick_freq;         /* RTOS tick 频率 */
    uint32_t oled_period_tick;  /* 页面刷新周期（tick） */
    uint32_t sample_period_tick;/* 电压采样周期（tick） */
    uint32_t next_oled_tick;    /* 下一次页面刷新触发 tick */
    uint32_t next_sample_tick;  /* 下一次电压采样触发 tick */

    float latest_voltage = 0.0f; /* 最新电压缓存值 */
    uint8_t mode_value;          /* DCC 模式值 */
    uint8_t run_state_value;     /* DCC 运行状态值 */

    mod_battery_ctx_t *battery_ctx = mod_battery_get_default_ctx(); /* Battery 默认上下文 */

    char voltage_label[] = "voltage:";    /* 电压标题 */
    char mode_label[] = "mode:";          /* 模式标题 */
    char task_on[] = "task:ON";           /* ON 状态文本 */
    char task_off[] = "task:OFF";         /* OFF 状态文本 */
    char task_prepare[] = "task:PREP";    /* PREPARE 状态文本 */
    char task_stop[] = "task:STOP";       /* STOP 状态文本 */

    (void)argument;

    /* 等待 InitTask 完成模块绑定。 */
    task_wait_init_done();

#if (TASK_OLED_STARTUP_ENABLE == 0U)
    /* 启动开关关闭：挂起当前任务，避免刷新显示。 */
    (void)osThreadSuspend(osThreadGetId());
    for (;;)
    {
        osDelay(osWaitForever);
    }
#endif

    /* 绑定 OLED I2C 通道并初始化屏幕。 */
    (void)OLED_BindI2C(&hi2c2, OLED_I2C_ADDR_DEFAULT, OLED_I2C_TIMEOUT_MS_DEFAULT);

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

    /* 步骤1：初始化 OLED 屏幕并读取一次上电电压。 */
    OLED_Init();
    if (mod_battery_update(battery_ctx))
    {
        latest_voltage = mod_battery_get_voltage(battery_ctx);
    }

    next_oled_tick = osKernelGetTickCount();
    next_sample_tick = next_oled_tick + sample_period_tick;

    for (;;)
    {
        uint32_t now_tick = osKernelGetTickCount(); /* 当前系统 tick */

        /* 步骤2：到采样时基时刷新电压缓存。 */
        if (task_oled_time_reached(now_tick, next_sample_tick))
        {
            if (mod_battery_update(battery_ctx))
            {
                latest_voltage = mod_battery_get_voltage(battery_ctx);
            }

            next_sample_tick += sample_period_tick;
            while (task_oled_time_reached(now_tick, next_sample_tick))
            {
                next_sample_tick += sample_period_tick;
            }
        }

        /* 步骤3：到刷新时基时重绘页面。 */
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

        /* 步骤4：空闲延时，避免任务空转占满 CPU。 */
        osDelay(TASK_OLED_IDLE_DELAY_TICK);
    }
}

