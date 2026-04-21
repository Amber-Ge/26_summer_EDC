/**
 * @file    drv_uart.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   UART 通用驱动接口。
 * @details
 * 1. 文件作用：为上层提供统一 UART 发送、DMA 接收、回调分发、端口索引映射能力。
 * 2. 解耦边界：驱动层只处理“字节流 + 事件”，不关心 VOFA/K230/Stepper 等协议语义。
 * 3. 上层绑定：协议层通过 `register_rx_callback + user_ctx` 绑定自己的上下文。
 * 4. 下层依赖：HAL UART/HAL UARTEx/HAL DMA。
 * 5. 兼容策略：保留旧版 `bool` 风格 API，新代码可优先使用 `*_ex` 状态码 API。
 */
#ifndef FINAL_GRADUATE_WORK_DRV_UART_H
#define FINAL_GRADUATE_WORK_DRV_UART_H

#include "usart.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 支持的 UART 端口数量。
 */
#define DRV_UART_PORT_COUNT (6U)

/**
 * @brief 旧版接收回调类型（兼容保留）。
 */
typedef void (*drv_uart_rx_callback_t)(uint16_t len);

/**
 * @brief 驱动状态码。
 */
typedef enum
{
    DRV_UART_OK = 0U,         // 操作成功
    DRV_UART_ERR_PARAM,       // 参数非法
    DRV_UART_ERR_UNSUPPORTED, // UART 实例不在受支持列表
    DRV_UART_ERR_BUSY,        // 通道忙（例如 TX 正在发送）
    DRV_UART_ERR_HAL          // HAL 调用失败
} drv_uart_status_t;

/**
 * @brief DMA 接收事件类型（由 HAL 事件映射而来）。
 */
typedef enum
{
    DRV_UART_RX_EVENT_IDLE = 0U, // 空闲线触发（一帧结束）
    DRV_UART_RX_EVENT_TC,        // DMA 传输完成
    DRV_UART_RX_EVENT_HT,        // DMA 半传输完成
    DRV_UART_RX_EVENT_UNKNOWN    // 未知事件
} drv_uart_rx_event_t;

/**
 * @brief 新版接收回调类型（推荐）。
 * @param huart 触发事件的 UART 句柄。
 * @param event 本次 RX 事件类型。
 * @param data DMA 缓冲区首地址（即 start 时传入的 p_buf）。
 * @param len 本次有效数据长度（字节）。
 * @param user_ctx 注册时透传的用户上下文指针。
 */
typedef void (*drv_uart_rx_callback_ex_t)(UART_HandleTypeDef *huart,
                                          drv_uart_rx_event_t event,
                                          const uint8_t *data,
                                          uint16_t len,
                                          void *user_ctx);

/**
 * @brief 将 UART 实例映射为固定端口索引。
 * @param instance UART 硬件实例指针。
 * @return int8_t 成功返回 [0, DRV_UART_PORT_COUNT-1]，失败返回 -1。
 */
int8_t drv_uart_get_port_index(USART_TypeDef *instance);

/**
 * @brief UART 驱动初始化。
 * @details
 * 清零内部端口上下文表；通常在系统初始化阶段调用一次。
 */
void drv_uart_init(void);

/**
 * @brief 阻塞发送单字节。
 */
void drv_uart_send_byte_blocking(UART_HandleTypeDef *huart, uint8_t data);

/**
 * @brief 阻塞发送字符串。
 */
void drv_uart_send_string_blocking(UART_HandleTypeDef *huart, const char *str, uint32_t timeout_ms);

/**
 * @brief 阻塞发送缓冲区。
 */
void drv_uart_send_buffer_blocking(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len, uint32_t timeout_ms);

/**
 * @brief 阻塞读取单字节。
 */
bool drv_uart_read_byte_blocking(UART_HandleTypeDef *huart, uint8_t *out, uint32_t timeout_ms);

/**
 * @brief 查询 TX 通道是否空闲。
 */
bool drv_uart_is_tx_free(UART_HandleTypeDef *huart);

/**
 * @brief DMA 发送（状态码版）。
 */
drv_uart_status_t drv_uart_send_dma_ex(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len);

/**
 * @brief DMA 发送（兼容 bool 版）。
 */
bool drv_uart_send_dma(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len);

/**
 * @brief 启动 ReceiveToIdle DMA 接收（状态码版）。
 */
drv_uart_status_t drv_uart_receive_dma_start_ex(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len);

/**
 * @brief 停止 DMA 接收（状态码版）。
 */
drv_uart_status_t drv_uart_receive_dma_stop_ex(UART_HandleTypeDef *huart);

/**
 * @brief 重启 DMA 接收（先 stop 再 start，状态码版）。
 */
drv_uart_status_t drv_uart_receive_dma_restart_ex(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len);

/**
 * @brief 启动 DMA 接收（兼容 bool 版）。
 */
bool drv_uart_receive_dma_start(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len);

/**
 * @brief 停止 DMA 接收（兼容 void 版）。
 */
void drv_uart_receive_dma_stop(UART_HandleTypeDef *huart);

/**
 * @brief 关闭 RX DMA 半传输中断（HT）。
 * @details
 * 适用于希望只处理 IDLE/TC 的协议场景，避免 HT 打断解析流程。
 */
drv_uart_status_t drv_uart_disable_rx_dma_half_transfer_irq(UART_HandleTypeDef *huart);

/**
 * @brief 注册新版接收回调（带 user_ctx）。
 * @details
 * 注册后将覆盖同端口上旧版回调。
 */
drv_uart_status_t drv_uart_register_rx_callback(UART_HandleTypeDef *huart,
                                                drv_uart_rx_callback_ex_t callback,
                                                void *user_ctx);

/**
 * @brief 注销接收回调（同时清除新旧回调槽位）。
 */
drv_uart_status_t drv_uart_unregister_rx_callback(UART_HandleTypeDef *huart);

/**
 * @brief 注册旧版接收回调（兼容保留）。
 * @details
 * 注册后将覆盖同端口上的新版回调。
 */
bool drv_uart_register_callback(UART_HandleTypeDef *huart, drv_uart_rx_callback_t callback);

#endif /* FINAL_GRADUATE_WORK_DRV_UART_H */

