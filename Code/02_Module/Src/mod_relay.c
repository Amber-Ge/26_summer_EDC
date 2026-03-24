/**
 * @file    mod_relay.c
 * @brief   继电器模块实现。
 * @details
 * 1. 文件作用：实现继电器映射绑定、状态检查和通道开关控制。
 * 2. 解耦边界：仅执行继电器动作，不承担蜂鸣节奏或激光启停策略。
 * 3. 上层绑定：`GpioTask` 等任务根据业务状态调用开关接口。
 * 4. 下层依赖：`drv_gpio` 执行引脚电平输出。
 */
#include "mod_relay.h"

#include <string.h>

/* 当前生效的继电器映射表 */
static mod_relay_hw_cfg_t s_relay_cfg_active[RELAY_MAX]; // 继电器硬件映射表的运行态副本。
/* 继电器模块绑定状态 */
static bool s_relay_cfg_bound = false; // 上下文或配置变量

/**
 * @brief 校验继电器ID是否合法
 */
/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param relay 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool is_valid_relay_id(mod_relay_id_e relay)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return ((relay >= RELAY_LASER) && (relay < RELAY_MAX));
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
 * @brief 校验单个继电器映射项
 */
/**
 * @brief 执行模块层设备控制与状态管理。
 * @param item 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool mod_relay_check_item(const mod_relay_hw_cfg_t *item)
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
bool mod_relay_bind_map(const mod_relay_hw_cfg_t *map, uint8_t map_num)
{
    // 1. 基础参数校验。
    if ((map == NULL) || (map_num != RELAY_MAX))
    {
        return false;
    }

    // 2. 逐项校验映射内容。
    for (uint8_t i = 0U; i < RELAY_MAX; i++) // 循环计数器
    {
        if (!mod_relay_check_item(&map[i]))
        {
            return false;
        }
    }

    // 3. 保存映射并标记绑定成功。
    memcpy(s_relay_cfg_active, map, sizeof(s_relay_cfg_active));
    s_relay_cfg_bound = true;

    return true;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 无。
 */
void mod_relay_unbind_map(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    s_relay_cfg_bound = false;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_relay_is_bound(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return s_relay_cfg_bound;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 无。
 */
void mod_relay_init(void)
{
    // 1. 未绑定时直接返回。
    if (!mod_relay_is_bound())
    {
        return;
    }

    // 2. 初始化时关闭所有继电器，保证上电安全。
    for (uint8_t i = 0U; i < RELAY_MAX; i++) // 循环计数器
    {
        mod_relay_off((mod_relay_id_e)i);
    }
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param relay 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
void mod_relay_on(mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg; // 模块变量，用于保存运行时状态。

    // 1. 参数和绑定状态检查。
    if (!is_valid_relay_id(relay) || !mod_relay_is_bound())
    {
        return;
    }

    // 2. 取出映射配置。
    cfg = &s_relay_cfg_active[relay];

    // 3. 输出有效电平，使继电器吸合。
    drv_gpio_write(cfg->port, cfg->pin, cfg->active_level);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param relay 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
void mod_relay_off(mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg; // 模块变量，用于保存运行时状态。

    // 1. 参数和绑定状态检查。
    if (!is_valid_relay_id(relay) || !mod_relay_is_bound())
    {
        return;
    }

    // 2. 取出映射配置。
    cfg = &s_relay_cfg_active[relay];

    // 3. 输出反相电平，使继电器断开。
    drv_gpio_write(cfg->port, cfg->pin, invert_level(cfg->active_level));
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param relay 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
void mod_relay_toggle(mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg; // 模块变量，用于保存运行时状态。

    // 1. 参数和绑定状态检查。
    if (!is_valid_relay_id(relay) || !mod_relay_is_bound())
    {
        return;
    }

    // 2. 取出映射配置。
    cfg = &s_relay_cfg_active[relay];

    // 3. 翻转当前电平。
    drv_gpio_toggle(cfg->port, cfg->pin);
}
