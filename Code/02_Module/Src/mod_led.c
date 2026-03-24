/**
 * @file    mod_led.c
 * @brief   LED 模块实现。
 * @details
 * 1. 文件作用：实现 LED 绑定表管理、边界检查和灯态控制执行。
 * 2. 解耦边界：本文件不承载闪烁节拍策略，仅执行“给定逻辑 ID 的灯态动作”。
 * 3. 上层绑定：`GpioTask` 等任务按业务状态机调用开关/翻转接口。
 * 4. 下层依赖：`drv_gpio` 完成电平写入，具体端口和有效电平由绑定表提供。
 */
#include "mod_led.h"

#include <string.h>

/* 当前生效的LED映射表 */
static mod_led_hw_cfg_t s_led_cfg_active[LED_MAX]; // LED 硬件映射表的运行态副本。
/* LED模块绑定状态 */
static bool s_led_cfg_bound = false; // 上下文或配置变量

/**
 * @brief 校验LED ID是否合法
 */
/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param led 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool is_valid_led_id(mod_led_id_e led)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return ((led >= LED_RED) && (led < LED_MAX));
}

/**
 * @brief 电平取反工具函数
 */
/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param level 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static gpio_level_e invert_level(gpio_level_e level)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return (level == GPIO_LEVEL_LOW) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

/**
 * @brief 校验单个LED映射项
 */
/**
 * @brief 执行模块层设备控制与状态管理。
 * @param item 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool mod_led_check_item(const mod_led_hw_cfg_t *item)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if (item == NULL)
    {
        return false;
    }

    if (item->port == NULL)
    {
        return false;
    }

    return true;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param map 函数输入参数，语义由调用场景决定。
 * @param map_num 数据长度或数量参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_led_bind_map(const mod_led_hw_cfg_t *map, uint8_t map_num)
{
    // 1. 基础参数校验。
    if ((map == NULL) || (map_num != LED_MAX))
    {
        return false;
    }

    // 2. 逐项校验映射内容。
    for (uint8_t i = 0U; i < LED_MAX; i++) // 循环计数器
    {
        if (!mod_led_check_item(&map[i]))
        {
            return false;
        }
    }

    // 3. 保存映射并标记绑定成功。
    memcpy(s_led_cfg_active, map, sizeof(s_led_cfg_active));
    s_led_cfg_bound = true;

    return true;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 无。
 */
void mod_led_unbind_map(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    s_led_cfg_bound = false;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_led_is_bound(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return s_led_cfg_bound;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 无。
 */
void mod_led_Init(void)
{
    // 1. 未绑定时直接返回，避免错误访问GPIO资源。
    if (!mod_led_is_bound())
    {
        return;
    }

    // 2. 初始化时关闭全部LED，保证上电状态可控。
    for (uint8_t i = 0U; i < LED_MAX; i++) // 循环计数器
    {
        mod_led_off((mod_led_id_e)i);
    }
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param led 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
void mod_led_on(mod_led_id_e led)
{
    const mod_led_hw_cfg_t *cfg; // 模块变量，用于保存运行时状态。

    // 1. 参数和绑定状态检查。
    if (!is_valid_led_id(led) || !mod_led_is_bound())
    {
        return;
    }

    // 2. 取出映射配置。
    cfg = &s_led_cfg_active[led];

    // 3. 输出有效电平，点亮LED。
    drv_gpio_write(cfg->port, cfg->pin, cfg->active_level);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param led 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
void mod_led_off(mod_led_id_e led)
{
    const mod_led_hw_cfg_t *cfg; // 模块变量，用于保存运行时状态。

    // 1. 参数和绑定状态检查。
    if (!is_valid_led_id(led) || !mod_led_is_bound())
    {
        return;
    }

    // 2. 取出映射配置。
    cfg = &s_led_cfg_active[led];

    // 3. 输出反相电平，熄灭LED。
    drv_gpio_write(cfg->port, cfg->pin, invert_level(cfg->active_level));
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param led 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
void mod_led_toggle(mod_led_id_e led)
{
    const mod_led_hw_cfg_t *cfg; // 模块变量，用于保存运行时状态。

    // 1. 参数和绑定状态检查。
    if (!is_valid_led_id(led) || !mod_led_is_bound())
    {
        return;
    }

    // 2. 取出映射配置。
    cfg = &s_led_cfg_active[led];

    // 3. 翻转当前电平。
    drv_gpio_toggle(cfg->port, cfg->pin);
}
