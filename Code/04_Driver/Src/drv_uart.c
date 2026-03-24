/**
 * @file    drv_uart.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   UART 通用驱动实现。
 * @details
 * 1. 文件作用：统一 UART DMA 收发、端口索引映射、接收事件分发与回调管理。
 * 2. 解耦边界：仅负责“字节流 + 事件”传输，不处理协议解析与任务业务。
 * 3. 上层绑定：VOFA/K230/Stepper 等模块通过注册回调与 user_ctx 完成挂接。
 * 4. 生命周期：Core 完成 UART 外设初始化后，模块层调用本驱动进行收发。
 */

#include "drv_uart.h"
#include <string.h>

/**
 * @brief 每个 UART 端口的驱动运行态。
 */
typedef struct
{
    uint8_t *rx_dma_buf;                    // 当前 ReceiveToIdle DMA 绑定缓冲
    uint16_t rx_dma_buf_len;                // DMA 缓冲容量（字节）
    drv_uart_rx_callback_ex_t rx_cb_ex;     // 新版回调槽
    void *rx_cb_user_ctx;                   // 新版回调用户上下文
    drv_uart_rx_callback_t rx_cb_legacy;    // 旧版回调槽（兼容）
} drv_uart_port_ctx_t;

static drv_uart_port_ctx_t s_port_ctx[DRV_UART_PORT_COUNT]; // UART 端口上下文表

/**
 * @brief 将 HAL 状态码映射为驱动状态码。
 */
static drv_uart_status_t _status_from_hal(HAL_StatusTypeDef hal_status)
{
    switch (hal_status)
    {
    case HAL_OK:
        return DRV_UART_OK;
    case HAL_BUSY:
        return DRV_UART_ERR_BUSY;
    default:
        return DRV_UART_ERR_HAL;
    }
}

/**
 * @brief 将 HAL RX 事件类型映射为驱动事件类型。
 */
static drv_uart_rx_event_t _map_rx_event(HAL_UART_RxEventTypeTypeDef hal_event)
{
    switch (hal_event)
    {
    case HAL_UART_RXEVENT_IDLE:
        return DRV_UART_RX_EVENT_IDLE;
    case HAL_UART_RXEVENT_TC:
        return DRV_UART_RX_EVENT_TC;
    case HAL_UART_RXEVENT_HT:
        return DRV_UART_RX_EVENT_HT;
    default:
        return DRV_UART_RX_EVENT_UNKNOWN;
    }
}

/**
 * @brief 根据 UART 句柄拿到端口上下文。
 * @param huart UART 句柄。
 * @param out_idx 输出端口索引（可选）。
 * @return drv_uart_port_ctx_t* 有效返回端口上下文，失败返回 NULL。
 */
static drv_uart_port_ctx_t *_get_port_ctx(UART_HandleTypeDef *huart, int8_t *out_idx)
{
    int8_t idx;

    if ((huart == NULL) || (huart->Instance == NULL))
    {
        return NULL;
    }

    idx = drv_uart_get_port_index(huart->Instance);
    if ((idx < 0) || (idx >= (int8_t)DRV_UART_PORT_COUNT))
    {
        return NULL;
    }

    if (out_idx != NULL)
    {
        *out_idx = idx;
    }

    return &s_port_ctx[(uint8_t)idx];
}

int8_t drv_uart_get_port_index(USART_TypeDef *instance)
{
    int8_t idx = -1;

    if (instance == NULL)
    {
        return -1;
    }

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

    return idx;
}

void drv_uart_init(void)
{
    memset(s_port_ctx, 0, sizeof(s_port_ctx));
}

void drv_uart_send_byte_blocking(UART_HandleTypeDef *huart, uint8_t data)
{
    drv_uart_send_buffer_blocking(huart, &data, 1U, 10U);
}

void drv_uart_send_string_blocking(UART_HandleTypeDef *huart, const char *str, uint32_t timeout_ms)
{
    if ((huart != NULL) && (str != NULL))
    {
        uint16_t len = (uint16_t)strlen(str);
        drv_uart_send_buffer_blocking(huart, (const uint8_t *)str, len, timeout_ms);
    }
}

void drv_uart_send_buffer_blocking(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    if ((huart == NULL) || (buf == NULL) || (len == 0U))
    {
        return;
    }

    if (huart->gState == HAL_UART_STATE_READY)
    {
        (void)HAL_UART_Transmit(huart, (uint8_t *)buf, len, timeout_ms);
    }
}

bool drv_uart_read_byte_blocking(UART_HandleTypeDef *huart, uint8_t *out, uint32_t timeout_ms)
{
    if ((huart == NULL) || (out == NULL))
    {
        return false;
    }

    return (HAL_UART_Receive(huart, out, 1U, timeout_ms) == HAL_OK);
}

bool drv_uart_is_tx_free(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return false;
    }

    return (huart->gState == HAL_UART_STATE_READY);
}

drv_uart_status_t drv_uart_send_dma_ex(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len)
{
    if ((huart == NULL) || (buf == NULL) || (len == 0U))
    {
        return DRV_UART_ERR_PARAM;
    }

    if (!drv_uart_is_tx_free(huart))
    {
        return DRV_UART_ERR_BUSY;
    }

    return _status_from_hal(HAL_UART_Transmit_DMA(huart, (uint8_t *)buf, len));
}

bool drv_uart_send_dma(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len)
{
    return (drv_uart_send_dma_ex(huart, buf, len) == DRV_UART_OK);
}

drv_uart_status_t drv_uart_receive_dma_start_ex(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len)
{
    drv_uart_port_ctx_t *port_ctx;
    drv_uart_status_t status;

    if ((huart == NULL) || (p_buf == NULL) || (max_len == 0U))
    {
        return DRV_UART_ERR_PARAM;
    }

    port_ctx = _get_port_ctx(huart, NULL);
    if (port_ctx == NULL)
    {
        return DRV_UART_ERR_UNSUPPORTED;
    }

    status = _status_from_hal(HAL_UARTEx_ReceiveToIdle_DMA(huart, p_buf, max_len));
    if (status == DRV_UART_OK)
    {
        port_ctx->rx_dma_buf = p_buf;
        port_ctx->rx_dma_buf_len = max_len;
    }

    return status;
}

drv_uart_status_t drv_uart_receive_dma_stop_ex(UART_HandleTypeDef *huart)
{
    drv_uart_port_ctx_t *port_ctx;

    if (huart == NULL)
    {
        return DRV_UART_ERR_PARAM;
    }

    port_ctx = _get_port_ctx(huart, NULL);
    if (port_ctx == NULL)
    {
        return DRV_UART_ERR_UNSUPPORTED;
    }

    port_ctx->rx_dma_buf = NULL;
    port_ctx->rx_dma_buf_len = 0U;
    return _status_from_hal(HAL_UART_AbortReceive(huart));
}

drv_uart_status_t drv_uart_receive_dma_restart_ex(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len)
{
    (void)drv_uart_receive_dma_stop_ex(huart);
    return drv_uart_receive_dma_start_ex(huart, p_buf, max_len);
}

bool drv_uart_receive_dma_start(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len)
{
    return (drv_uart_receive_dma_start_ex(huart, p_buf, max_len) == DRV_UART_OK);
}

void drv_uart_receive_dma_stop(UART_HandleTypeDef *huart)
{
    (void)drv_uart_receive_dma_stop_ex(huart);
}

drv_uart_status_t drv_uart_disable_rx_dma_half_transfer_irq(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return DRV_UART_ERR_PARAM;
    }

    if (huart->hdmarx == NULL)
    {
        return DRV_UART_ERR_PARAM;
    }

    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
    return DRV_UART_OK;
}

drv_uart_status_t drv_uart_register_rx_callback(UART_HandleTypeDef *huart,
                                                drv_uart_rx_callback_ex_t callback,
                                                void *user_ctx)
{
    drv_uart_port_ctx_t *port_ctx;

    if (huart == NULL)
    {
        return DRV_UART_ERR_PARAM;
    }

    port_ctx = _get_port_ctx(huart, NULL);
    if (port_ctx == NULL)
    {
        return DRV_UART_ERR_UNSUPPORTED;
    }

    port_ctx->rx_cb_ex = callback;
    port_ctx->rx_cb_user_ctx = user_ctx;
    port_ctx->rx_cb_legacy = NULL;
    return DRV_UART_OK;
}

drv_uart_status_t drv_uart_unregister_rx_callback(UART_HandleTypeDef *huart)
{
    drv_uart_port_ctx_t *port_ctx;

    if (huart == NULL)
    {
        return DRV_UART_ERR_PARAM;
    }

    port_ctx = _get_port_ctx(huart, NULL);
    if (port_ctx == NULL)
    {
        return DRV_UART_ERR_UNSUPPORTED;
    }

    port_ctx->rx_cb_ex = NULL;
    port_ctx->rx_cb_user_ctx = NULL;
    port_ctx->rx_cb_legacy = NULL;
    return DRV_UART_OK;
}

bool drv_uart_register_callback(UART_HandleTypeDef *huart, drv_uart_rx_callback_t callback)
{
    drv_uart_port_ctx_t *port_ctx;

    if (huart == NULL)
    {
        return false;
    }

    port_ctx = _get_port_ctx(huart, NULL);
    if (port_ctx == NULL)
    {
        return false;
    }

    port_ctx->rx_cb_legacy = callback;
    port_ctx->rx_cb_ex = NULL;
    port_ctx->rx_cb_user_ctx = NULL;
    return true;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    drv_uart_port_ctx_t *port_ctx;
    drv_uart_rx_event_t rx_event;
    uint16_t dispatch_len = Size;

    port_ctx = _get_port_ctx(huart, NULL);
    if (port_ctx == NULL)
    {
        return;
    }

    if ((port_ctx->rx_dma_buf_len > 0U) && (dispatch_len > port_ctx->rx_dma_buf_len))
    {
        dispatch_len = port_ctx->rx_dma_buf_len;
    }

    rx_event = _map_rx_event(HAL_UARTEx_GetRxEventType(huart));

    if (port_ctx->rx_cb_ex != NULL)
    {
        port_ctx->rx_cb_ex(huart,
                           rx_event,
                           (const uint8_t *)port_ctx->rx_dma_buf,
                           dispatch_len,
                           port_ctx->rx_cb_user_ctx);
        return;
    }

    if (port_ctx->rx_cb_legacy != NULL)
    {
        port_ctx->rx_cb_legacy(dispatch_len);
    }
}

