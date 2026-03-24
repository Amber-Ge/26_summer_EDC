/**
 * @file    drv_uart.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   UART 驱动层接口定义。
 * @details
 * 1. 文件作用：提供 UART 阻塞/非阻塞收发、DMA 收发和接收回调注册能力。
 * 2. 解耦边界：驱动层只处理字节流传输，不解析协议帧、不做业务命令分发。
 * 3. 上层绑定：`mod_vofa`、`mod_k230`、`mod_stepper` 等模块复用该传输接口。
 * 4. 下层依赖：依赖 HAL UART 与 DMA 接口，串口句柄来源于 `usart.h`。
 * 5. 生命周期：句柄必须由 Core 初始化完成；回调注册后由中断/DMA 驱动触发。
 */
#ifndef FINAL_GRADUATE_WORK_DRV_UART_H
#define FINAL_GRADUATE_WORK_DRV_UART_H

#include "usart.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief UART 接收回调函数类型。
 * @details
 * 当串口收到一帧数据并触发接收事件时，驱动层会调用该回调函数。
 * @param len 本次收到的数据长度（字节）。
 * @return 无返回值。
 */
typedef void (*drv_uart_rx_callback_t)(uint16_t len);

/**
 * @brief UART 驱动初始化接口。
 * @details
 * 当前版本保留该接口用于后续扩展；若无特殊初始化需求，可为空实现。
 * @return 无返回值。
 */
void drv_uart_init(void);

/**
 * @brief 阻塞发送单字节。
 * @param huart UART 句柄指针。
 * @param data 待发送字节。
 * @return 无返回值。
 */
void drv_uart_send_byte_blocking(UART_HandleTypeDef *huart, uint8_t data);

/**
 * @brief 阻塞发送字符串。
 * @param huart UART 句柄指针。
 * @param str 待发送字符串（以 `\0` 结尾）。
 * @param timeout_ms 阻塞超时时间（ms）。
 * @return 无返回值。
 */
void drv_uart_send_string_blocking(UART_HandleTypeDef *huart, const char *str, uint32_t timeout_ms);

/**
 * @brief 阻塞发送缓冲区。
 * @param huart UART 句柄指针。
 * @param buf 待发送数据缓冲区。
 * @param len 发送长度（字节）。
 * @param timeout_ms 阻塞超时时间（ms）。
 * @return 无返回值。
 */
void drv_uart_send_buffer_blocking(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len, uint32_t timeout_ms);

/**
 * @brief 阻塞读取单字节。
 * @param huart UART 句柄指针。
 * @param out 输出参数，用于返回读取到的字节。
 * @param timeout_ms 阻塞超时时间（ms）。
 * @return true 读取成功。
 * @return false 参数无效或读取失败/超时。
 */
bool drv_uart_read_byte_blocking(UART_HandleTypeDef *huart, uint8_t *out, uint32_t timeout_ms);

/**
 * @brief 判断 UART 发送通道是否空闲。
 * @param huart UART 句柄指针。
 * @return true 当前可启动发送。
 * @return false 当前忙或参数无效。
 */
bool drv_uart_is_tx_free(UART_HandleTypeDef *huart);

/**
 * @brief 启动 DMA 异步发送。
 * @details
 * 调用成功后 DMA 在后台发送数据，函数立即返回。
 * @param huart UART 句柄指针。
 * @param buf 发送数据缓冲区。
 * @param len 发送长度（字节）。
 * @return true DMA 启动成功。
 * @return false 参数无效或通道忙。
 */
bool drv_uart_send_dma(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len);

/**
 * @brief 启动 DMA 异步接收（IDLE 帧结束判定）。
 * @details
 * 接收完成后会进入 HAL 接收事件回调，并分发给已注册用户回调。
 * @param huart UART 句柄指针。
 * @param p_buf 接收缓冲区地址。
 * @param max_len 缓冲区最大长度（字节）。
 * @return true 接收启动成功。
 * @return false 参数无效或启动失败。
 */
bool drv_uart_receive_dma_start(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len);

/**
 * @brief 停止 UART 异步接收。
 * @param huart UART 句柄指针。
 * @return 无返回值。
 */
void drv_uart_receive_dma_stop(UART_HandleTypeDef *huart);

/**
 * @brief 注册 UART 接收回调函数。
 * @details
 * 驱动层会根据串口实例分发接收事件到对应回调。
 * @param huart UART 句柄指针。
 * @param callback 用户接收回调函数。
 * @return true 注册成功。
 * @return false 参数无效或串口不受支持。
 */
bool drv_uart_register_callback(UART_HandleTypeDef *huart, drv_uart_rx_callback_t callback);

#endif /* FINAL_GRADUATE_WORK_DRV_UART_H */
