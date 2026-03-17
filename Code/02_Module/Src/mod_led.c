#include "mod_led.h"

#include <string.h>

/* 当前生效的LED映射表 */
static mod_led_hw_cfg_t s_led_cfg_active[LED_MAX];
/* LED模块绑定状态 */
static bool s_led_cfg_bound = false;

/**
 * @brief 校验LED ID是否合法
 */
static bool is_valid_led_id(mod_led_id_e led)
{
    return ((led >= LED_RED) && (led < LED_MAX));
}

/**
 * @brief 电平取反工具函数
 */
static gpio_level_e invert_level(gpio_level_e level)
{
    return (level == GPIO_LEVEL_LOW) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

/**
 * @brief 校验单个LED映射项
 */
static bool mod_led_check_item(const mod_led_hw_cfg_t *item)
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

bool mod_led_bind_map(const mod_led_hw_cfg_t *map, uint8_t map_num)
{
    // 1. 基础参数校验。
    if ((map == NULL) || (map_num != LED_MAX))
    {
        return false;
    }

    // 2. 逐项校验映射内容。
    for (uint8_t i = 0U; i < LED_MAX; i++)
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

void mod_led_unbind_map(void)
{
    s_led_cfg_bound = false;
}

bool mod_led_is_bound(void)
{
    return s_led_cfg_bound;
}

void mod_led_Init(void)
{
    // 1. 未绑定时直接返回，避免错误访问GPIO资源。
    if (!mod_led_is_bound())
    {
        return;
    }

    // 2. 初始化时关闭全部LED，保证上电状态可控。
    for (uint8_t i = 0U; i < LED_MAX; i++)
    {
        mod_led_off((mod_led_id_e)i);
    }
}

void mod_led_on(mod_led_id_e led)
{
    const mod_led_hw_cfg_t *cfg;

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

void mod_led_off(mod_led_id_e led)
{
    const mod_led_hw_cfg_t *cfg;

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

void mod_led_toggle(mod_led_id_e led)
{
    const mod_led_hw_cfg_t *cfg;

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