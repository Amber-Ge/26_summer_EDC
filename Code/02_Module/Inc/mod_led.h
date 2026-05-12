/**
 * @file    mod_led.h
 * @author  Jiang Kaizhong
 * @version v1.00
 * @date    2026-03-24
 * @brief   LED module interface based on ctx architecture.
 * @details
 * 1. Maps logical LED channels to GPIO pins and exposes unified control APIs.
 * 2. Task layer uses semantic LED IDs and does not access HAL GPIO directly.
 * 3. Lifecycle: ctx_init -> bind -> init -> on/off/toggle -> unbind/deinit.
 */
#ifndef FINAL_GRADUATE_WORK_MOD_LED_H
#define FINAL_GRADUATE_WORK_MOD_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"

/**
 * @brief Logical LED channel IDs.
 */
typedef enum
{
    LED_STATE = 0, // Status indicator LED
    LED_BROAD,     // Board debug LED
    LED_MAX        // Number of LED channels
} mod_led_id_e;

/**
 * @brief Hardware mapping for one LED channel.
 */
typedef struct
{
    drv_gpio_pin_t pin;        // Bound GPIO pin
    gpio_level_e active_level; // Active output level
} mod_led_hw_cfg_t;

/**
 * @brief Binding parameters for the LED module.
 */
typedef struct
{
    const mod_led_hw_cfg_t *map; // Mapping table base address
    uint8_t map_num;             // Mapping table size, must equal LED_MAX
} mod_led_bind_t;

/**
 * @brief Runtime context of the LED module.
 */
typedef struct
{
    bool inited;                   // Context initialized flag
    bool bound;                    // Mapping bound flag
    mod_led_hw_cfg_t map[LED_MAX]; // Active mapping copy
} mod_led_ctx_t;

mod_led_ctx_t *mod_led_get_default_ctx(void);
bool mod_led_ctx_init(mod_led_ctx_t *ctx, const mod_led_bind_t *bind);
void mod_led_ctx_deinit(mod_led_ctx_t *ctx);
bool mod_led_bind(mod_led_ctx_t *ctx, const mod_led_bind_t *bind);
void mod_led_unbind(mod_led_ctx_t *ctx);
bool mod_led_is_bound(const mod_led_ctx_t *ctx);
void mod_led_init(mod_led_ctx_t *ctx);
void mod_led_on(mod_led_ctx_t *ctx, mod_led_id_e led);
void mod_led_off(mod_led_ctx_t *ctx, mod_led_id_e led);
void mod_led_toggle(mod_led_ctx_t *ctx, mod_led_id_e led);

#endif /* FINAL_GRADUATE_WORK_MOD_LED_H */
