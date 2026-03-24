/**
 * @file    mod_key.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   按键模块接口（模块层负责板级按键绑定与事件语义映射）。
 * @details
 * 1. 文件作用：绑定板级按键引脚，执行消抖/时序判定并输出模块级事件语义。
 * 2. 解耦边界：本模块只负责“按键输入语义化”，不直接操作任务状态机和业务动作。
 * 3. 上层绑定：`KeyTask` 以固定周期调用扫描接口，并将事件转换为信号量通知。
 * 4. 下层依赖：使用 `drv_key` 的状态机能力与 `drv_gpio` 的电平读取能力。
 * 5. 生命周期：先 `init` 后 `scan`，扫描周期应与宏配置时序保持一致。
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
