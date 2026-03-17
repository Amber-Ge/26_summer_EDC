/**
 ******************************************************************************
 * @file    mod_relay.h
 * @brief   继电器模块接口定义
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_RELAY_H
#define FINAL_GRADUATE_WORK_MOD_RELAY_H // 头文件防重复包含宏

#include "drv_gpio.h"

/**
 * @brief 继电器逻辑 ID。
 */
typedef enum
{
    RELAY_LASER = 0, // 激光继电器通道
    RELAY_MAX // 继电器通道数量上限
} mod_relay_id_e;

/**
 * @brief 初始化继电器模块。
 * @details 默认将所有继电器输出设置为关闭状态。
 */
void mod_relay_init(void);

/**
 * @brief 打开指定继电器。
 * @param relay 继电器 ID。
 */
void mod_relay_on(mod_relay_id_e relay);

/**
 * @brief 关闭指定继电器。
 * @param relay 继电器 ID。
 */
void mod_relay_off(mod_relay_id_e relay);

/**
 * @brief 翻转指定继电器状态。
 * @param relay 继电器 ID。
 */
void mod_relay_toggle(mod_relay_id_e relay);

#endif /* FINAL_GRADUATE_WORK_MOD_RELAY_H */
