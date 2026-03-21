#include "task_oled.h"
#include "adc.h"
#include "i2c.h"
#include "mod_oled.h"
#include "task_dcc.h"
#include "task_init.h"

static uint8_t task_oled_time_reached(uint32_t now, uint32_t target)
{
    return (uint8_t)((int32_t)(now - target) >= 0);
}

void StartOledTask(void *argument)
{
    uint32_t tick_freq;
    uint32_t oled_period_tick;
    uint32_t sample_period_tick;
    uint32_t next_oled_tick;
    uint32_t next_sample_tick;
    float latest_voltage = 0.0f;
    uint8_t mode_value;
    uint8_t run_state_value;

    char voltage_label[] = "voltage:";
    char mode_label[] = "mode:";
    char task_on[] = "task:ON";
    char task_off[] = "task:OFF";
    char task_prepare[] = "task:PREP";
    char task_stop[] = "task:STOP";

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

    for (;;)
    {
        uint32_t now_tick = osKernelGetTickCount();

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
