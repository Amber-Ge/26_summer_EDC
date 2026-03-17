/**
 ******************************************************************************
 * @file    mod_key.c
 * @brief   按键模块实现（板级绑定层）
 ******************************************************************************
 */

#include "mod_key.h"

#include "drv_gpio.h"
#include "drv_key.h"
#include "main.h"

/** 模块管理的按键数量 */
#define MOD_KEY_NUM (3U)

/**
 * @brief 单个按键硬件绑定描述
 */
typedef struct
{
    GPIO_TypeDef *port;                // GPIO 端口
    uint16_t pin;                      // GPIO 引脚
    gpio_level_e active_level;         // 按下有效电平

    mod_key_event_e click_event;       // 单击事件映射
    mod_key_event_e double_event;      // 双击事件映射
    mod_key_event_e long_event;        // 长按事件映射
} mod_key_hw_cfg_t;

/**
 * @brief 板级按键映射表
 * @note 这里是“模块层绑定引脚”的唯一入口。
 */
static const mod_key_hw_cfg_t s_mod_key_hw[MOD_KEY_NUM] =
{
    {
        KEY_1_GPIO_Port,
        KEY_1_Pin,
        GPIO_LEVEL_LOW,
        MOD_KEY_EVENT_1_CLICK,
        MOD_KEY_EVENT_1_DOUBLE_CLICK,
        MOD_KEY_EVENT_1_LONG_PRESS
    },
    {
        KEY_2_GPIO_Port,
        KEY_2_Pin,
        GPIO_LEVEL_LOW,
        MOD_KEY_EVENT_2_CLICK,
        MOD_KEY_EVENT_2_DOUBLE_CLICK,
        MOD_KEY_EVENT_2_LONG_PRESS
    },
    {
        KEY_3_GPIO_Port,
        KEY_3_Pin,
        GPIO_LEVEL_LOW,
        MOD_KEY_EVENT_3_CLICK,
        MOD_KEY_EVENT_3_DOUBLE_CLICK,
        MOD_KEY_EVENT_3_LONG_PRESS
    }
};

/**
 * @brief 毫秒转扫描 tick（向上取整）
 * @param ms 毫秒值
 * @return uint16_t 对应 tick 数
 */
static uint16_t mod_key_ms_to_ticks(uint16_t ms)
{
    uint16_t ticks; // 计算得到的 tick 数

    // 1. 防止扫描周期被错误配置为 0。
    if (MOD_KEY_SCAN_PERIOD_MS == 0U)
    {
        return 1U;
    }

    // 2. 按“向上取整”把毫秒换算成 tick，保证时间阈值不被提前触发。
    ticks = (uint16_t)((ms + MOD_KEY_SCAN_PERIOD_MS - 1U) / MOD_KEY_SCAN_PERIOD_MS);

    // 3. 保证最小返回 1 tick，避免出现 0 tick 的无效配置。
    if (ticks == 0U)
    {
        ticks = 1U;
    }

    // 4. 返回换算结果。
    return ticks;
}

/**
 * @brief 提供给 drv_key 的按键读取回调
 * @param key_id 按键索引
 * @param user_arg 用户上下文（本模块暂未使用）
 * @return true 当前按键按下
 * @return false 当前按键未按下
 */
static bool mod_key_read_cb(uint8_t key_id, void *user_arg)
{
    gpio_level_e current_level; // 当前采样到的引脚电平

    // 1. 当前模块不使用 user_arg，显式置空避免编译告警。
    (void)user_arg;

    // 2. 索引保护：超范围时直接返回未按下。
    if (key_id >= MOD_KEY_NUM)
    {
        return false;
    }

    // 3. 读取当前 key_id 对应引脚的电平。
    current_level = drv_gpio_read(s_mod_key_hw[key_id].port, s_mod_key_hw[key_id].pin);

    // 4. 与配置的有效电平比较，返回是否“按下”。
    return (current_level == s_mod_key_hw[key_id].active_level);
}

void mod_key_init(void)
{
    drv_key_cfg_t cfg; // drv_key 初始化配置

    // 1. 配置按键数量。
    cfg.key_num = MOD_KEY_NUM;

    // 2. 配置消抖 tick（由 ms 转换而来）。
    cfg.debounce_ticks = mod_key_ms_to_ticks(MOD_KEY_DEBOUNCE_MS);

    // 3. 配置长按 tick（传 0 可关闭长按功能）。
    cfg.long_press_ticks = mod_key_ms_to_ticks(MOD_KEY_LONG_PRESS_MS);

    // 4. 配置双击窗口 tick（传 0 可关闭双击功能）。
    cfg.double_click_ticks = mod_key_ms_to_ticks(MOD_KEY_DOUBLE_CLICK_MS);

    // 5. 配置按键读取回调。
    cfg.read_cb = mod_key_read_cb;

    // 6. 本模块无额外上下文，传 NULL 即可。
    cfg.user_arg = NULL;

    // 7. 调用通用按键驱动初始化。
    (void)drv_key_init(&cfg);
}

mod_key_event_e mod_key_scan(void)
{
    drv_key_event_t drv_evt; // 驱动层事件

    // 1. 先调用驱动层扫描。
    if (!drv_key_scan(&drv_evt))
    {
        // 1.1 扫描失败（通常是未初始化），返回无事件。
        return MOD_KEY_EVENT_NONE;
    }

    // 2. 如果驱动层本轮无事件，直接返回无事件。
    if (drv_evt.type == DRV_KEY_EVENT_NONE)
    {
        return MOD_KEY_EVENT_NONE;
    }

    // 3. 索引越界保护：任何非法 key_id 统一返回无事件。
    if (drv_evt.key_id >= MOD_KEY_NUM)
    {
        return MOD_KEY_EVENT_NONE;
    }

    // 4. 根据驱动层事件类型做模块层语义映射。
    switch (drv_evt.type)
    {
    case DRV_KEY_EVENT_CLICK:
        // 4.1 单击映射。
        return s_mod_key_hw[drv_evt.key_id].click_event;

    case DRV_KEY_EVENT_DOUBLE_CLICK:
        // 4.2 双击映射。
        return s_mod_key_hw[drv_evt.key_id].double_event;

    case DRV_KEY_EVENT_LONG_PRESS:
        // 4.3 长按映射。
        return s_mod_key_hw[drv_evt.key_id].long_event;

    case DRV_KEY_EVENT_NONE:
    default:
        // 4.4 兜底保护。
        return MOD_KEY_EVENT_NONE;
    }
}
