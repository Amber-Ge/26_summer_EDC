/**
 ******************************************************************************
 * @file    drv_key.h
 * @brief   按键驱动层接口定义
 * @details
 * 采用轮询 + 状态机消抖方式，将按键 GPIO 电平转换为单次“按下事件”。
 * 本驱动仅输出事件，不处理业务动作。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_DRV_KEY_H
#define FINAL_GRADUATE_WORK_DRV_KEY_H

#include "main.h"
#include <stdint.h>

/**
 * @brief 按键事件类型定义。
 */
typedef enum
{
    DRV_KEY_EVENT_NONE = 0,      // 无按键事件
    DRV_KEY_EVENT_1_PRESSED,     // KEY1 按下事件
    DRV_KEY_EVENT_2_PRESSED,     // KEY2 按下事件
    DRV_KEY_EVENT_3_PRESSED      // KEY3 按下事件
} DrvKeyEvent_e;

/**
 * @brief 初始化按键驱动状态。
 * @details
 * 轮询方案下 GPIO 由 CubeMX 初始化，本函数主要用于复位内部状态机变量。
 */
void drv_key_init(void);

/**
 * @brief 扫描按键并返回本轮事件。
 * @details
 * 建议由上层每 10ms 调用一次该函数。函数内部执行消抖状态机，
 * 返回“本轮确认按下事件”，并保证一次按下只上报一次。
 *
 * @return DrvKeyEvent_e
 * - `DRV_KEY_EVENT_NONE`：本轮无事件
 * - `DRV_KEY_EVENT_X_PRESSED`：对应按键确认按下
 */
DrvKeyEvent_e drv_key_scan(void);

#endif /* FINAL_GRADUATE_WORK_DRV_KEY_H */
