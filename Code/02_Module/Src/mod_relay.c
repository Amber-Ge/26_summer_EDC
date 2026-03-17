#include "mod_relay.h"

#include <string.h>

/* 当前生效的继电器映射表 */
static mod_relay_hw_cfg_t s_relay_cfg_active[RELAY_MAX];
/* 继电器模块绑定状态 */
static bool s_relay_cfg_bound = false;

/**
 * @brief 校验继电器ID是否合法
 */
static bool is_valid_relay_id(mod_relay_id_e relay)
{
    return ((relay >= RELAY_LASER) && (relay < RELAY_MAX));
}

/**
 * @brief 电平取反工具函数
 */
static gpio_level_e invert_level(gpio_level_e level)
{
    return (level == GPIO_LEVEL_LOW) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

/**
 * @brief 校验单个继电器映射项
 */
static bool mod_relay_check_item(const mod_relay_hw_cfg_t *item)
{
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

bool mod_relay_bind_map(const mod_relay_hw_cfg_t *map, uint8_t map_num)
{
    // 1. 基础参数校验。
    if ((map == NULL) || (map_num != RELAY_MAX))
    {
        return false;
    }

    // 2. 逐项校验映射内容。
    for (uint8_t i = 0U; i < RELAY_MAX; i++)
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

void mod_relay_unbind_map(void)
{
    s_relay_cfg_bound = false;
}

bool mod_relay_is_bound(void)
{
    return s_relay_cfg_bound;
}

void mod_relay_init(void)
{
    // 1. 未绑定时直接返回。
    if (!mod_relay_is_bound())
    {
        return;
    }

    // 2. 初始化时关闭所有继电器，保证上电安全。
    for (uint8_t i = 0U; i < RELAY_MAX; i++)
    {
        mod_relay_off((mod_relay_id_e)i);
    }
}

void mod_relay_on(mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg;

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

void mod_relay_off(mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg;

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

void mod_relay_toggle(mod_relay_id_e relay)
{
    const mod_relay_hw_cfg_t *cfg;

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