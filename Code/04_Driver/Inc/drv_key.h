/**
 ******************************************************************************
 * @file    drv_key.h
 * @brief   通用按键驱动接口（驱动层不绑定任何具体引脚）
 * @details
 * 1. 驱动层只负责“按键判定逻辑”：消抖、单击、双击、长按。
 * 2. 驱动层不关心具体 GPIO，由上层通过回调提供“是否按下”。
 * 3. 驱动层不承载业务语义（例如“切换模式”），只输出通用事件。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_DRV_KEY_H
#define FINAL_GRADUATE_WORK_DRV_KEY_H

#include <stdbool.h>
#include <stdint.h>

/** 驱动最多管理的按键数量上限 */
#define DRV_KEY_MAX_NUM (16U)

/** 默认消抖计数（单位：扫描 tick） */
#define DRV_KEY_DEFAULT_DEBOUNCE_TICKS (2U)

/** 默认长按阈值（单位：扫描 tick） */
#define DRV_KEY_DEFAULT_LONG_PRESS_TICKS (70U)

/** 默认双击间隔（单位：扫描 tick） */
#define DRV_KEY_DEFAULT_DOUBLE_CLICK_TICKS (25U)

/**
 * @brief 驱动层按键事件类型（通用事件）
 */
typedef enum
{
    DRV_KEY_EVENT_NONE = 0U,            // 无事件
    DRV_KEY_EVENT_CLICK = 1U,           // 单击
    DRV_KEY_EVENT_DOUBLE_CLICK = 2U,    // 双击
    DRV_KEY_EVENT_LONG_PRESS = 3U       // 长按
} drv_key_event_type_e;

/**
 * @brief 驱动层事件结构体
 */
typedef struct
{
    uint8_t key_id;                  // 按键索引：0 ~ key_num-1
    drv_key_event_type_e type;       // 事件类型
} drv_key_event_t;

/**
 * @brief 读取按键状态的回调函数类型
 * @param key_id   按键索引
 * @param user_arg 用户自定义上下文
 * @return true  当前按键是“按下态”
 * @return false 当前按键是“未按下态”
 */
typedef bool (*drv_key_read_cb_t)(uint8_t key_id, void *user_arg);

/**
 * @brief 驱动初始化参数
 */
typedef struct
{
    uint8_t key_num;                    // 按键数量
    uint16_t debounce_ticks;            // 消抖确认计数（tick）
    uint16_t long_press_ticks;          // 长按阈值（tick），传 0 表示关闭长按
    uint16_t double_click_ticks;        // 双击间隔（tick），传 0 表示关闭双击
    drv_key_read_cb_t read_cb;          // 按键读取回调
    void *user_arg;                     // 回调上下文
} drv_key_cfg_t;

/**
 * @brief 初始化通用按键驱动
 * @param cfg 初始化配置
 * @return true  初始化成功
 * @return false 参数非法
 */
bool drv_key_init(const drv_key_cfg_t *cfg);

/**
 * @brief 扫描一次按键状态机
 * @param out_event 输出事件（无事件时 type=DRV_KEY_EVENT_NONE）
 * @return true  扫描执行成功
 * @return false 驱动未初始化或参数非法
 */
bool drv_key_scan(drv_key_event_t *out_event);

#endif /* FINAL_GRADUATE_WORK_DRV_KEY_H */
