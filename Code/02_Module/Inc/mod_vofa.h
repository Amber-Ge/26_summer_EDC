/**
 * @file    mod_vofa.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   VOFA 通信模块接口。
 * @details
 * 1. 文件作用：封装 VOFA 协议通道的发送、接收、命令解析与上下文管理。
 * 2. 解耦边界：本模块处理通信协议和缓冲，不直接执行上层业务命令动作。
 * 3. 上层绑定：`InitTask` 注入串口与互斥资源，业务任务按需调用发送/取命令接口。
 * 4. 下层依赖：`drv_uart` 负责收发，`mod_uart_guard` 负责 UART 独占仲裁，RTOS 对象用于同步。
 * 5. 生命周期：上下文需先初始化/绑定，再在主循环中调用 `process` 驱动协议状态机。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_VOFA_H
#define FINAL_GRADUATE_WORK_MOD_VOFA_H

#include "drv_uart.h"
#include "common_str.h"
#include "cmsis_os2.h"
#include <stdbool.h>
#include <stdint.h>
#include "mod_uart_guard.h"
#include <string.h>



#define MOD_VOFA_TX_BUF_SIZE            (256U)
#define MOD_VOFA_RX_BUF_SIZE            (64U)
#define MOD_VOFA_MAX_BIND_SEM           (4U)
#define MOD_VOFA_TX_MUTEX_TIMEOUT_MS    (5U)

typedef enum
{
    VOFA_CMD_NONE = 0, // 未识别到有效命令，或当前接收缓存中没有命令数据。
    VOFA_CMD_START,    // 解析到 start 命令（兼容旧协议保留字段，当前流程可不使用）。
    VOFA_CMD_STOP,     // 解析到 stop 命令（兼容旧协议保留字段，当前流程可不使用）。
    VOFA_CMD_MAX       // 命令枚举上界标记，不是有效命令值。
} vofa_cmd_id_t;

typedef struct
{
    UART_HandleTypeDef *huart; // 绑定串口句柄（必填）。VOFA 的 DMA 收发都通过该串口完成。
    osSemaphoreId_t sem_list[MOD_VOFA_MAX_BIND_SEM]; // 数据/事件通知信号量列表。收到命令或数据后可逐个 release 通知上层任务。
    uint8_t sem_count; // sem_list 的有效元素数量。仅 [0, sem_count) 范围被视为已绑定对象。
    osMutexId_t tx_mutex; // 发送互斥锁句柄（可选）。多任务并发发送 VOFA 文本时用于串口互斥保护。
} mod_vofa_bind_t;

typedef struct
{
    bool inited; // 上下文是否已初始化。ctx_init 成功后为 true，未初始化不允许 bind/send/get_command。
    bool bound; // 上下文是否已完成绑定。true 代表已占用 UART 并完成接收回调链路配置。
    mod_vofa_bind_t bind; // 当前生效绑定参数副本。bind 成功时写入，unbind/deinit 时会被清理。
    vofa_cmd_id_t last_cmd; // 最近一次解析到的命令结果。上层可轮询该字段获取最新命令状态。
    char tx_buf[MOD_VOFA_TX_BUF_SIZE]; // 文本发送缓冲区。格式化后的 VOFA 文本先写入这里，再启动 DMA 发送。
    uint8_t rx_buf[MOD_VOFA_RX_BUF_SIZE]; // DMA 接收缓冲区。串口接收回调将基于该缓存解析命令字符串。
} mod_vofa_ctx_t;

/* ========================== [ Ctx-based API ] ========================== */

/**
 * @brief 获取默认上下文实例。
 */
mod_vofa_ctx_t *mod_vofa_get_default_ctx(void);

/**
 * @brief 初始化 VOFA 上下文，可选立即绑定。
 * @param ctx 目标上下文。
 * @param bind 可选绑定参数；传 NULL 则仅初始化。
 * @return true 成功；false 失败。
 */
bool mod_vofa_ctx_init(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind);

/**
 * @brief 反初始化上下文并释放已绑定 UART 资源。
 * @param ctx 目标上下文。
 */
void mod_vofa_ctx_deinit(mod_vofa_ctx_t *ctx);

/**
 * @brief 绑定上下文到指定 UART 与同步资源。
 * @param ctx 目标上下文。
 * @param bind 绑定参数（huart 必填）。
 * @return true 成功；false 失败。
 */
bool mod_vofa_bind(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind);

/**
 * @brief 解绑上下文，停止接收并释放 UART 归属权。
 * @param ctx 目标上下文。
 */
void mod_vofa_unbind(mod_vofa_ctx_t *ctx);

/**
 * @brief 判断上下文是否处于可工作绑定状态。
 */
bool mod_vofa_is_bound(const mod_vofa_ctx_t *ctx);

/**
 * @brief 追加一个通知信号量。
 * @details
 * 同一信号量重复添加视为成功，不会重复占用槽位。
 */
bool mod_vofa_add_semaphore(mod_vofa_ctx_t *ctx, osSemaphoreId_t sem_id);

/**
 * @brief 移除一个通知信号量。
 * @details
 * 若信号量不存在返回 false；存在则删除并压缩列表。
 */
bool mod_vofa_remove_semaphore(mod_vofa_ctx_t *ctx, osSemaphoreId_t sem_id);

/**
 * @brief 清空全部通知信号量绑定。
 */
void mod_vofa_clear_semaphores(mod_vofa_ctx_t *ctx);

/**
 * @brief 配置发送互斥锁。
 * @param mutex_id 互斥锁句柄；传 NULL 表示关闭发送互斥保护。
 */
void mod_vofa_set_tx_mutex(mod_vofa_ctx_t *ctx, osMutexId_t mutex_id);

/**
 * @brief 读取并清空最近一次命令解析结果。
 * @return vofa_cmd_id_t 最近一次命令；无命令返回 `VOFA_CMD_NONE`。
 */
vofa_cmd_id_t mod_vofa_get_command_ctx(mod_vofa_ctx_t *ctx);

/**
 * @brief 发送浮点数组，格式为 `tag:v1,v2,...\\n`。
 */
bool mod_vofa_send_float_ctx(mod_vofa_ctx_t *ctx, const char *tag, const float *arr, uint16_t n);

/**
 * @brief 发送有符号整型数组，格式为 `tag:v1,v2,...\\n`。
 */
bool mod_vofa_send_int_ctx(mod_vofa_ctx_t *ctx, const char *tag, const int32_t *arr, uint16_t n);

/**
 * @brief 发送无符号整型数组，格式为 `tag:v1,v2,...\\n`。
 */
bool mod_vofa_send_uint_ctx(mod_vofa_ctx_t *ctx, const char *tag, const uint32_t *arr, uint16_t n);

/**
 * @brief 发送原始字符串，并自动追加换行符。
 */
bool mod_vofa_send_string_ctx(mod_vofa_ctx_t *ctx, const char *str);

/* ========================== [ Legacy Compatible API ] ========================== */

/**
 * @brief 兼容初始化接口（默认上下文）。
 */
void mod_vofa_init(UART_HandleTypeDef *huart, osSemaphoreId_t sem_id);

/**
 * @brief 兼容命令读取接口（默认上下文）。
 */
vofa_cmd_id_t mod_vofa_get_command(void);

/**
 * @brief 兼容浮点数组发送接口（默认上下文）。
 */
bool mod_vofa_send_float(const char *tag, const float *arr, uint16_t n);

/**
 * @brief 兼容有符号整型数组发送接口（默认上下文）。
 */
bool mod_vofa_send_int(const char *tag, const int32_t *arr, uint16_t n);

/**
 * @brief 兼容无符号整型数组发送接口（默认上下文）。
 */
bool mod_vofa_send_uint(const char *tag, const uint32_t *arr, uint16_t n);

/**
 * @brief 兼容字符串发送接口（默认上下文）。
 */
bool mod_vofa_send_string(const char *str);

#endif



