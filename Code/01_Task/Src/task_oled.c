#include "task_oled.h"
#include "mod_oled.h"

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
    uint32_t tick_freq;         // 系统内核的 Tick 频率 (1秒包含多少个Tick，通常1s有1000个tick)
    uint32_t oled_period_tick;   // 换算成 Tick 的屏幕刷新周期
    uint32_t sample_period_tick; // 换算成 Tick 的电池采样周期
    uint32_t next_oled_tick;     // 存储下一次刷屏的具体时间点
    uint32_t next_sample_tick;   // 存储下一次采样的具体时间点

    /* --- 变量定义：数据处理相关 --- */
    float latest_voltage = 0.0f; // 缓存从电池模块读取的最新的电压值
    char voltage_label[] = "voltage:"; // 屏幕显示的固定文本

    (void)argument; // 显式声明参数未使用，避免编译器警告

    /* 1. 获取系统频率（通常为 1000Hz）并计算周期对应的 Tick 数 */
    tick_freq = osKernelGetTickFreq();
    if (tick_freq == 0U)
        tick_freq = 1000U;

    oled_period_tick = TASK_OLED_REFRESH_S * tick_freq;
    sample_period_tick = TASK_OLED_SAMPLE_S * tick_freq;

    /* 2. 防御性编程：确保周期至少为 1，防止除零或死循环 */
    if (oled_period_tick == 0U)
        oled_period_tick = 1U;
    if (sample_period_tick == 0U)
        sample_period_tick = 1U;

    /* 3. 硬件与数据初始化 */
    OLED_Init();

    if (mod_battery_update()) // 初始更新一次电池数据
        latest_voltage = mod_battery_get_voltage();

    /* 4. 设定任务运行的初始时间戳起点 */
    next_oled_tick = osKernelGetTickCount();
    next_sample_tick = next_oled_tick + sample_period_tick;

    /* 5. 任务主循环 */
    for (;;)
    {
        uint32_t now_tick = osKernelGetTickCount(); // 获取当前时刻

        /* --- 逻辑板块 A: 电池采样 (每 5 秒触发一次) --- */
        if (task_oled_time_reached(now_tick, next_sample_tick))
        {
            if (mod_battery_update()) // 从 ADC 读取最新电压
                latest_voltage = mod_battery_get_voltage();

            // 更新下一次采样的时间轴
            next_sample_tick += sample_period_tick;

            // 补偿逻辑：如果系统发生卡顿导致错过多次，循环将时间轴对齐到未来
            while (task_oled_time_reached(now_tick, next_sample_tick))
                next_sample_tick += sample_period_tick;
        }

        /* --- 逻辑板块 B: OLED 刷新 (每 1 秒触发一次) --- */
        if (task_oled_time_reached(now_tick, next_oled_tick))
        {
            OLED_Clear(); // 清空显存缓冲区
            OLED_ShowString(0U, 0U, voltage_label, OLED_8X16); // 画出 "voltage:" 标签
            OLED_ShowFloatNum(0U, 16U, latest_voltage, 2U, 2U, OLED_8X16); // 画出电压数值
            OLED_Update(); // 将缓冲区数据一次性通过 I2C 发送给屏幕

            // 更新下一次刷屏的时间轴
            next_oled_tick += oled_period_tick;
            while (task_oled_time_reached(now_tick, next_oled_tick))
            {
                next_oled_tick += oled_period_tick;
            }
        }

        /* --- 逻辑板块 C: 任务延时 --- */
        /* 让出 CPU 使用权 200 个 Tick，允许其他低优先级任务运行，降低系统功耗 */
        osDelay(TASK_OLED_IDLE_DELAY_TICK);
    }
}