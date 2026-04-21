/**
 * @file    mod_uart_guard.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   UART 资源占用仲裁接口定义。
 * @details
 * 1. 文件作用：维护 UART 归属者与 claim-depth，避免跨模块冲突占用。
 * 2. 解耦边界：仅管理“谁拥有 UART 资源”，不处理发送、接收、协议解析。
 * 3. 上层绑定：VOFA/K230/Stepper 在 bind/unbind 阶段调用 claim/release。
 * 4. 下层依赖：依赖 `drv_uart_get_port_index` 进行端口统一映射。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_UART_GUARD_H
#define FINAL_GRADUATE_WORK_MOD_UART_GUARD_H

#include "drv_uart.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief UART 资源拥有者枚举。
 */
typedef enum
{
    MOD_UART_OWNER_NONE = 0U,    // 无拥有者
    MOD_UART_OWNER_VOFA = 1U,    // VOFA 模块
    MOD_UART_OWNER_K230 = 2U,    // K230 模块
    MOD_UART_OWNER_STEPPER = 3U  // Stepper 模块
} mod_uart_owner_e;

/**
 * @brief 申请 UART 资源归属权。
 * @details
 * 兼容接口：按 owner 维度重入 claim。
 * 推荐新代码使用 `mod_uart_guard_claim_ctx` 传入 ctx 指针。
 */
bool mod_uart_guard_claim(UART_HandleTypeDef *huart, mod_uart_owner_e owner);

/**
 * @brief 释放 UART 资源归属权。
 * @details
 * 兼容接口：按 owner 维度释放。
 * 推荐新代码使用 `mod_uart_guard_release_ctx` 传入 ctx 指针。
 */
bool mod_uart_guard_release(UART_HandleTypeDef *huart, mod_uart_owner_e owner);

/**
 * @brief 申请 UART 资源归属权（带 claimant 指针）。
 * @details
 * claimant 建议传模块 ctx 指针，用于区分“同 owner 的不同实例”。
 * 仅允许“同 owner + 同 claimant”重入 claim。
 */
bool mod_uart_guard_claim_ctx(UART_HandleTypeDef *huart, mod_uart_owner_e owner, const void *claimant);

/**
 * @brief 释放 UART 资源归属权（带 claimant 指针）。
 * @details
 * 仅允许与申请时相同的 owner + claimant 释放。
 */
bool mod_uart_guard_release_ctx(UART_HandleTypeDef *huart, mod_uart_owner_e owner, const void *claimant);

/**
 * @brief 查询 UART 当前拥有者。
 */
mod_uart_owner_e mod_uart_guard_get_owner(UART_HandleTypeDef *huart);

/**
 * @brief 查询 UART 当前 claim-depth。
 * @return uint8_t 0 表示未被占用。
 */
uint8_t mod_uart_guard_get_claim_depth(UART_HandleTypeDef *huart);

/**
 * @brief 查询 UART 当前 claimant 指针。
 * @return const void* 未占用时返回 NULL。
 */
const void *mod_uart_guard_get_claimant(UART_HandleTypeDef *huart);

#endif /* FINAL_GRADUATE_WORK_MOD_UART_GUARD_H */

