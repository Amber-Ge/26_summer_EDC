#include "task_oled.h"
#include "task_init.h"
#include "task_dcc.h"
#include "mod_oled.h"
#include "adc.h"
#include "i2c.h"

/**
 * @brief 时间到达判断函数（支持 tick 回绕）
 * @param now 当前 tick
 * @param target 目标 tick
 * @return uint8_t 1=到达，0=未到达
 */
static uint8_t task_oled_time_reached(uint32_t now, uint32_t target)
{
    return (uint8_t)((int32_t)(now - target) >= 0);
}

/**
 * @brief OLED 显示任务入口
 * @param argument 任务参数（当前未使用）
 *
 * @details
 * 当前显示内容固定为四行（8x16 字体）：
 * 1. voltage 标题
 * 2. 电压值
 * 3. mode 值
 * 4. task 状态（映射 DCC ready：ON/OFF）
 */
void StartOledTask(void *argument)
{
    uint32_t tick_freq;            // RTOS tick 频率（Hz）
    uint32_t oled_period_tick;     // OLED 刷新周期（tick）
    uint32_t sample_period_tick;   // 电压采样周期（tick）
    uint32_t next_oled_tick;       // 下一次 OLED 刷新时间点
    uint32_t next_sample_tick;     // 下一次电压采样时间点
    float latest_voltage = 0.0f;   // 最近一次成功采样得到的电压值
    uint8_t mode_value;            // DCC mode 快照
    uint8_t ready_value;           // DCC ready 快照

    char voltage_label[] = "voltage:"; // 第1行标题
    char mode_label[] = "mode:";       // 第3行标题
    char task_on[] = "task:ON";        // ready=1 文本
    char task_off[] = "task:OFF";      // ready=0 文本

    (void)argument; // 显式忽略未使用参数

    // 启动门控：等待 InitTask 完成全局初始化
    task_wait_init_done();

    // 绑定 OLED I2C 与电池 ADC
    (void)OLED_BindI2C(&hi2c2, OLED_I2C_ADDR_DEFAULT, OLED_I2C_TIMEOUT_MS_DEFAULT);
    (void)mod_battery_bind_adc(&hadc1);

    // 读取 tick 频率，异常时回退到 1000Hz
    tick_freq = osKernelGetTickFreq();
    if (tick_freq == 0U)
    {
        tick_freq = 1000U;
    }

    // OLED刷新周期：由毫秒转换成tick（向上取整，确保不小于目标周期）
    oled_period_tick = (uint32_t)(((uint64_t)TASK_OLED_REFRESH_MS * (uint64_t)tick_freq + 999ULL) / 1000ULL);

    // 采样周期：秒转tick
    sample_period_tick = TASK_OLED_SAMPLE_S * tick_freq;

    // 周期下限保护：至少1 tick
    if (oled_period_tick == 0U)
    {
        oled_period_tick = 1U;
    }
    if (sample_period_tick == 0U)
    {
        sample_period_tick = 1U;
    }

    // 初始化OLED并先采样一次电压
    OLED_Init();
    if (mod_battery_update())
    {
        latest_voltage = mod_battery_get_voltage();
    }

    // 初始化时间轴
    next_oled_tick = osKernelGetTickCount();
    next_sample_tick = next_oled_tick + sample_period_tick;

    for (;;)
    {
        uint32_t now_tick = osKernelGetTickCount(); // 当前系统tick

        // 到采样时间则更新电压缓存
        if (task_oled_time_reached(now_tick, next_sample_tick))
        {
            if (mod_battery_update())
            {
                latest_voltage = mod_battery_get_voltage();
            }

            // 若任务落后多个采样周期，补齐采样时间轴
            next_sample_tick += sample_period_tick;
            while (task_oled_time_reached(now_tick, next_sample_tick))
            {
                next_sample_tick += sample_period_tick;
            }
        }

        // 到刷新时间则重绘显示
        if (task_oled_time_reached(now_tick, next_oled_tick))
        {
            // 读取 DCC 状态快照
            mode_value = task_dcc_get_mode();
            ready_value = task_dcc_get_ready();

            OLED_Clear();

            // 第1行：voltage 标题
            OLED_ShowString(0U, 0U, voltage_label, OLED_8X16);

            // 第2行：电压值
            OLED_ShowFloatNum(0U, 16U, latest_voltage, 2U, 2U, OLED_8X16);

            // 第3行：mode 值
            OLED_ShowString(0U, 32U, mode_label, OLED_8X16);
            OLED_ShowNum(48U, 32U, (uint32_t)mode_value, 1U, OLED_8X16);

            // 第4行：task ON/OFF
            if (ready_value != 0U)
            {
                OLED_ShowString(0U, 48U, task_on, OLED_8X16);
            }
            else
            {
                OLED_ShowString(0U, 48U, task_off, OLED_8X16);
            }

            OLED_Update();

            // 若任务落后多个刷新周期，补齐刷新时间轴
            next_oled_tick += oled_period_tick;
            while (task_oled_time_reached(now_tick, next_oled_tick))
            {
                next_oled_tick += oled_period_tick;
            }
        }

        // 短延时让出CPU，提升显示响应同时避免空转
        osDelay(TASK_OLED_IDLE_DELAY_TICK);
    }
}
