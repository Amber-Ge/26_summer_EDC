/**
 * @file    drv_uart.c
 * @brief   UART 驱动层接口实现。
 * @details
 * 1. 文件作用：实现阻塞收发、DMA 异步收发与回调分发，统一串口访问方式。
 * 2. 解耦边界：仅传输字节流并分发回调，不解析具体协议帧。
 * 3. 上层绑定：VOFA/K230/Stepper 模块调用发送与接收接口并注册回调。
 * 4. 下层依赖：HAL_UART/HAL_UARTEx 接口与 DMA 中断回调入口。
 */

#include "drv_uart.h"

/* ========================== [ 1. 内部私有资源 ] ========================== */

static drv_uart_rx_callback_t s_rx_callbacks[6] = {NULL}; // 串口接收回调登记表：0~5 对应 USART1/2/3/UART4/5/USART6

/**
 * @brief 将串口硬件实例映射为回调表索引。
 * @param instance 串口硬件实例地址。
 * @return int8_t 有效实例返回 0~5，未知实例返回 -1。
 */
static int8_t _get_uart_index(USART_TypeDef *instance)
{
    int8_t idx = -1; // 串口实例对应的数组索引，默认无效

    // 1. 根据硬件实例地址匹配预定义索引，避免上层关心具体串口编号。
    switch ((uint32_t)instance)
    {
    case (uint32_t)USART1:
        idx = 0;
        break;
    case (uint32_t)USART2:
        idx = 1;
        break;
    case (uint32_t)USART3:
        idx = 2;
        break;
    case (uint32_t)UART4:
        idx = 3;
        break;
    case (uint32_t)UART5:
        idx = 4;
        break;
    case (uint32_t)USART6:
        idx = 5;
        break;
    default:
        idx = -1;
        break;
    }

    // 2. 返回映射结果。
    return idx;
}

/* ========================== [ 2. 阻塞式通信接口 ] ========================== */

/**
 * @brief 阻塞发送单字节。
 * @param huart UART 句柄指针。
 * @param data 待发送字节。
 * @return 无返回值。
 */
void drv_uart_send_byte_blocking(UART_HandleTypeDef *huart, uint8_t data)
{
    // 1. 复用缓冲区发送接口，避免重复实现发送流程。
    drv_uart_send_buffer_blocking(huart, &data, 1U, 10U);
}

/**
 * @brief 阻塞发送字符串。
 * @param huart UART 句柄指针。
 * @param str 待发送字符串。
 * @param timeout_ms 阻塞超时时间（ms）。
 * @return 无返回值。
 */
void drv_uart_send_string_blocking(UART_HandleTypeDef *huart, const char *str, uint32_t timeout_ms)
{
    // 1. 先做空指针保护，避免非法参数触发访问异常。
    if ((huart != NULL) && (str != NULL))
    {
        uint16_t len = (uint16_t)strlen(str); // 字符串长度（字节）

        // 2. 使用统一缓冲区接口发送，确保时序与状态检查一致。
        drv_uart_send_buffer_blocking(huart, (uint8_t *)str, len, timeout_ms);
    }
}

/**
 * @brief 阻塞发送缓冲区。
 * @param huart UART 句柄指针。
 * @param buf 待发送数据缓冲区。
 * @param len 发送长度（字节）。
 * @param timeout_ms 阻塞超时时间（ms）。
 * @return 无返回值。
 */
void drv_uart_send_buffer_blocking(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    // 1. 参数校验：句柄、缓冲区和长度必须有效。
    if ((huart != NULL) && (buf != NULL) && (len > 0U))
    {
        // 2. 仅在发送状态空闲时启动阻塞发送，避免与进行中的发送冲突。
        if (huart->gState == HAL_UART_STATE_READY)
        {
            (void)HAL_UART_Transmit(huart, (uint8_t *)buf, len, timeout_ms);
        }
    }
}

/**
 * @brief 阻塞读取单字节。
 * @param huart UART 句柄指针。
 * @param out 输出参数，用于返回读取到的字节。
 * @param timeout_ms 阻塞超时时间（ms）。
 * @return true 读取成功。
 * @return false 参数无效或读取失败。
 */
bool drv_uart_read_byte_blocking(UART_HandleTypeDef *huart, uint8_t *out, uint32_t timeout_ms)
{
    bool result = false; // 读取结果，默认失败

    // 1. 参数校验：句柄与输出缓冲区不能为空。
    if ((huart != NULL) && (out != NULL))
    {
        // 2. 调用 HAL 阻塞读取 1 字节并记录状态。
        if (HAL_UART_Receive(huart, out, 1U, timeout_ms) == HAL_OK)
        {
            result = true;
        }
    }

    // 3. 返回读取结果。
    return result;
}

/* ========================== [ 3. 异步 DMA 发送 (TX) ] ========================== */

/**
 * @brief 判断 UART 发送通道是否空闲。
 * @param huart UART 句柄指针。
 * @return true 当前可启动发送。
 * @return false 当前忙或参数无效。
 */
bool drv_uart_is_tx_free(UART_HandleTypeDef *huart)
{
    bool is_free = false; // 发送空闲标志，默认忙

    // 1. 参数校验后读取 HAL 发送状态机。
    if (huart != NULL)
    {
        // 2. 仅在 HAL 报告 READY 时返回可发送。
        if (huart->gState == HAL_UART_STATE_READY)
        {
            is_free = true;
        }
    }

    // 3. 返回空闲状态。
    return is_free;
}

/**
 * @brief 启动 DMA 异步发送。
 * @param huart UART 句柄指针。
 * @param buf 发送数据缓冲区。
 * @param len 发送长度（字节）。
 * @return true DMA 启动成功。
 * @return false 参数无效、通道忙或 HAL 启动失败。
 */
bool drv_uart_send_dma(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len)
{
    bool result = false; // DMA 发送启动结果，默认失败

    // 1. 参数校验：句柄、缓冲区和发送长度必须有效。
    if ((huart != NULL) && (buf != NULL) && (len != 0U))
    {
        // 2. 仅在发送状态空闲时启动 DMA，避免与其他发送流程冲突。
        if (huart->gState == HAL_UART_STATE_READY)
        {
            if (HAL_UART_Transmit_DMA(huart, (uint8_t *)buf, len) == HAL_OK)
            {
                result = true;
            }
        }
    }

    // 3. 返回启动结果。
    return result;
}

/* ========================== [ 4. 异步 DMA 接收 (RX) ] ========================== */

/**
 * @brief 启动 DMA 异步接收（IDLE 帧结束判定）。
 * @param huart UART 句柄指针。
 * @param p_buf 接收缓冲区地址。
 * @param max_len 接收缓冲区最大长度。
 * @return true 接收启动成功。
 * @return false 参数无效或 HAL 启动失败。
 */
bool drv_uart_receive_dma_start(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len)
{
    bool result = false; // DMA 接收启动结果，默认失败

    // 1. 参数校验：句柄、缓冲区和长度必须有效。
    if ((huart != NULL) && (p_buf != NULL) && (max_len > 0U))
    {
        // 2. 启动 ReceiveToIdle DMA，让 HAL 在帧结束时触发接收事件。
        if (HAL_UARTEx_ReceiveToIdle_DMA(huart, p_buf, max_len) == HAL_OK)
        {
            result = true;
        }
    }

    // 3. 返回启动结果。
    return result;
}

/**
 * @brief 停止 UART 异步接收。
 * @param huart UART 句柄指针。
 * @return 无返回值。
 */
void drv_uart_receive_dma_stop(UART_HandleTypeDef *huart)
{
    // 1. 参数有效时中止当前接收，避免访问空句柄。
    if (huart != NULL)
    {
        (void)HAL_UART_AbortReceive(huart);
    }
}

/* ========================== [ 5. 回调注册与分发中心 ] ========================== */

/**
 * @brief 注册 UART 接收回调函数。
 * @param huart UART 句柄指针。
 * @param callback 用户接收回调函数。
 * @return true 注册成功。
 * @return false 参数无效或实例不受支持。
 */
bool drv_uart_register_callback(UART_HandleTypeDef *huart, drv_uart_rx_callback_t callback)
{
    bool result = false; // 回调注册结果，默认失败

    // 1. 参数校验后把实例映射为回调表索引。
    if (huart != NULL)
    {
        int8_t idx = _get_uart_index(huart->Instance); // 串口实例索引

        // 2. 索引有效时登记回调并返回成功。
        if (idx != -1)
        {
            s_rx_callbacks[idx] = callback;
            result = true;
        }
    }

    // 3. 返回注册结果。
    return result;
}

/**
 * @brief HAL 串口接收事件回调（ReceiveToIdle DMA）。
 * @param huart 发生事件的串口句柄。
 * @param Size 本次实际接收到的字节数。
 * @return 无返回值。
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    // 1. 参数校验，避免空句柄访问。
    if (huart != NULL)
    {
        int8_t idx = _get_uart_index(huart->Instance); // 串口实例索引

        // 2. 索引与回调都有效时，把本次接收长度分发给上层。
        if ((idx != -1) && (s_rx_callbacks[idx] != NULL))
        {
            s_rx_callbacks[idx](Size);
        }
    }
}
