/**
 ******************************************************************************
 * @file    mod_key.h
 * @brief   按键模块接口（模块层负责板级按键绑定与事件语义映射）
 * @details
 * 1. 模块层绑定具体引脚（KEY_1/KEY_2/KEY_3）。
 * 2. 模块层把驱动层通用事件映射成模块层业务可读事件。
 * 3. 模块层提供统一时序宏，任务层扫描周期与按键判定使用同一组宏。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_KEY_H
#define FINAL_GRADUATE_WORK_MOD_KEY_H

/* ========================= 按键时序配置宏（单位：ms） ========================= */

/** 按键扫描周期（任务层应与此保持一致） */
#define MOD_KEY_SCAN_PERIOD_MS (10U)

/** 按下/释放消抖时间 */
#define MOD_KEY_DEBOUNCE_MS (20U)

/** 长按触发时间 */
#define MOD_KEY_LONG_PRESS_MS (700U)

/** 双击判定窗口 */
#define MOD_KEY_DOUBLE_CLICK_MS (250U)

/* ========================= 模块层事件定义 ========================= */

/**
 * @brief 模块层按键事件
 */
typedef enum
{
    MOD_KEY_EVENT_NONE = 0U,             // 无事件

    MOD_KEY_EVENT_1_CLICK,               // KEY1 单击
    MOD_KEY_EVENT_2_CLICK,               // KEY2 单击
    MOD_KEY_EVENT_3_CLICK,               // KEY3 单击

    MOD_KEY_EVENT_1_DOUBLE_CLICK,        // KEY1 双击
    MOD_KEY_EVENT_2_DOUBLE_CLICK,        // KEY2 双击
    MOD_KEY_EVENT_3_DOUBLE_CLICK,        // KEY3 双击

    MOD_KEY_EVENT_1_LONG_PRESS,          // KEY1 长按
    MOD_KEY_EVENT_2_LONG_PRESS,          // KEY2 长按
    MOD_KEY_EVENT_3_LONG_PRESS           // KEY3 长按
} mod_key_event_e;

/**
 * @brief 初始化按键模块
 */
void mod_key_init(void);

/**
 * @brief 扫描按键并返回模块层事件
 * @return mod_key_event_e 本轮事件
 */
mod_key_event_e mod_key_scan(void);

#endif /* FINAL_GRADUATE_WORK_MOD_KEY_H */
