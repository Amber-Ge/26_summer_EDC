/**
 ******************************************************************************
 * @file    mod_uart_guard.h
 * @brief   UART 资源占用仲裁接口定义
 * @details
 * 该模块用于协调多个业务模块对同一 UART 的独占访问，避免冲突。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_UART_GUARD_H
#define FINAL_GRADUATE_WORK_MOD_UART_GUARD_H // 头文件防重复包含宏

#include "drv_uart.h"
#include <stdbool.h>

/**
 * @brief UART 资源拥有者枚举。
 */
typedef enum
{
    MOD_UART_OWNER_NONE = 0U, // 无拥有者
    MOD_UART_OWNER_VOFA = 1U, // VOFA模块占用
    MOD_UART_OWNER_K230 = 2U, // K230模块占用
    MOD_UART_OWNER_STEPPER = 3U // 步进电机模块占用
} mod_uart_owner_e;

/**
 * @brief 申请 UART 资源归属权。
 * @param huart UART 句柄指针。
 * @param owner 申请者 ID。
 * @return true 申请成功。
 * @return false 申请失败（参数无效或已被他人占用）。
 */
bool mod_uart_guard_claim(UART_HandleTypeDef *huart, mod_uart_owner_e owner);

/**
 * @brief 释放 UART 资源归属权。
 * @param huart UART 句柄指针。
 * @param owner 释放者 ID（需与当前拥有者一致）。
 * @return true 释放成功。
 * @return false 释放失败（参数无效或拥有者不匹配）。
 */
bool mod_uart_guard_release(UART_HandleTypeDef *huart, mod_uart_owner_e owner);

/**
 * @brief 查询 UART 当前拥有者。
 * @param huart UART 句柄指针。
 * @return mod_uart_owner_e 当前拥有者 ID。
 */
mod_uart_owner_e mod_uart_guard_get_owner(UART_HandleTypeDef *huart);

#endif /* FINAL_GRADUATE_WORK_MOD_UART_GUARD_H */
