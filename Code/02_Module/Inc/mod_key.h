/**
 * @file    mod_key.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   按键模块接口（ctx 架构）。
 * @details
 * 1. 文件作用：提供按键映射绑定、驱动事件语义映射和模块级事件输出能力。
 * 2. 解耦边界：模块层只负责“事件语义映射”，不承担任务调度与业务状态机。
 * 3. 上层调用：Task 层按固定周期调用 `mod_key_scan(ctx)` 获取事件。
 * 4. 下层依赖：通过 `drv_key` 做去抖/单击/双击/长按判定，通过 `drv_gpio` 读取电平。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_KEY_H
#define FINAL_GRADUATE_WORK_MOD_KEY_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_gpio.h"
#include "main.h"

/* ========================= 按键时序配置（单位：ms） ========================= */

#define MOD_KEY_SCAN_PERIOD_MS (10U)
#define MOD_KEY_DEBOUNCE_MS (20U)
#define MOD_KEY_LONG_PRESS_MS (700U)
#define MOD_KEY_DOUBLE_CLICK_MS (250U)

#define MOD_KEY_MAX_NUM (8U)

/**
 * @brief 模块层按键事件定义。
 */
typedef enum
{
    MOD_KEY_EVENT_NONE = 0U,
    MOD_KEY_EVENT_1_CLICK,
    MOD_KEY_EVENT_2_CLICK,
    MOD_KEY_EVENT_3_CLICK,
    MOD_KEY_EVENT_1_DOUBLE_CLICK,
    MOD_KEY_EVENT_2_DOUBLE_CLICK,
    MOD_KEY_EVENT_3_DOUBLE_CLICK,
    MOD_KEY_EVENT_1_LONG_PRESS,
    MOD_KEY_EVENT_2_LONG_PRESS,
    MOD_KEY_EVENT_3_LONG_PRESS
} mod_key_event_e;

/**
 * @brief 单路按键映射配置。
 */
typedef struct
{
    GPIO_TypeDef *port;           // GPIO 端口句柄
    uint16_t pin;                 // GPIO 引脚掩码
    gpio_level_e active_level;    // 按下有效电平
    mod_key_event_e click_event;  // 单击事件映射
    mod_key_event_e double_event; // 双击事件映射
    mod_key_event_e long_event;   // 长按事件映射
} mod_key_hw_cfg_t;

/**
 * @brief 按键绑定输入参数。
 */
typedef struct
{
    const mod_key_hw_cfg_t *map; // 按键映射表首地址
    uint8_t key_num;             // 按键数量，范围 1~MOD_KEY_MAX_NUM
} mod_key_bind_t;

/**
 * @brief 按键模块运行上下文。
 */
typedef struct
{
    bool inited;                           // 上下文是否已初始化
    bool bound;                            // 上下文是否已完成绑定
    uint8_t key_num;                       // 当前生效按键数量
    mod_key_hw_cfg_t map[MOD_KEY_MAX_NUM]; // 当前生效映射表副本
} mod_key_ctx_t;

/**
 * @brief 获取模块默认上下文。
 * @return mod_key_ctx_t* 默认上下文地址。
 */
mod_key_ctx_t *mod_key_get_default_ctx(void);

/**
 * @brief 初始化按键上下文，可选直接绑定映射。
 * @param ctx 目标上下文。
 * @param bind 可选绑定参数；传 NULL 表示仅初始化。
 * @return true 初始化成功。
 * @return false 参数非法或绑定失败。
 */
bool mod_key_ctx_init(mod_key_ctx_t *ctx, const mod_key_bind_t *bind);

/**
 * @brief 反初始化按键上下文。
 * @param ctx 目标上下文。
 */
void mod_key_ctx_deinit(mod_key_ctx_t *ctx);

/**
 * @brief 绑定按键映射并初始化底层按键驱动。
 * @param ctx 目标上下文。
 * @param bind 绑定参数。
 * @return true 绑定成功。
 * @return false 参数非法、资源冲突或驱动初始化失败。
 */
bool mod_key_bind(mod_key_ctx_t *ctx, const mod_key_bind_t *bind);

/**
 * @brief 解绑按键上下文。
 * @param ctx 目标上下文。
 */
void mod_key_unbind(mod_key_ctx_t *ctx);

/**
 * @brief 查询按键上下文是否已就绪。
 * @param ctx 目标上下文。
 * @return true 已就绪。
 * @return false 未就绪。
 */
bool mod_key_is_bound(const mod_key_ctx_t *ctx);

/**
 * @brief 扫描一次按键并输出模块事件。
 * @param ctx 目标上下文。
 * @return mod_key_event_e 本次扫描得到的模块事件。
 */
mod_key_event_e mod_key_scan(mod_key_ctx_t *ctx);

#endif /* FINAL_GRADUATE_WORK_MOD_KEY_H */
