/**
 * @file    mod_led.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   LED 模块接口。
 * @details
 * 1. 文件作用：维护 LED 逻辑 ID 到板级 GPIO 的映射，并提供点亮/熄灭/翻转接口。
 * 2. 解耦边界：本模块只抽象“灯态控制”，不承担闪烁节拍、告警策略等任务层逻辑。
 * 3. 上层绑定：`GpioTask` 等业务任务按状态机调用 LED 控制接口。
 * 4. 下层依赖：`drv_gpio` 完成最终电平写入，硬件映射通过 bind 接口注入。
 * 5. 生命周期：先 `bind_map` 再 `Init`，运行期可 `is_bound` 检查绑定合法性。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_LED_H
#define FINAL_GRADUATE_WORK_MOD_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"

typedef enum
{
    LED_RED = 0,
    LED_GREEN,
    LED_YELLOW,
    LED_MAX
} mod_led_id_e;

/** 单路LED硬件绑定配置 */
typedef struct
{
    GPIO_TypeDef *port;      // LED 端口
    uint16_t pin;            // LED 引脚
    gpio_level_e active_level; // 点亮有效电平
} mod_led_hw_cfg_t;

bool mod_led_bind_map(const mod_led_hw_cfg_t *map, uint8_t map_num);
void mod_led_unbind_map(void);
bool mod_led_is_bound(void);

void mod_led_Init(void);
void mod_led_on(mod_led_id_e led);
void mod_led_off(mod_led_id_e led);
void mod_led_toggle(mod_led_id_e led);

#endif /* FINAL_GRADUATE_WORK_MOD_LED_H */


