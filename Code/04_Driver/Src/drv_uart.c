#include "drv_uart.h"

/* ========================== [ 1. 内部私有资源 ] ========================== */

// 串口回调登记表：0:USART1, 1:USART2, 2:USART3, 3:UART4, 4:UART5, 5:USART6
static drv_uart_rx_callback_t s_rx_callbacks[6] = {NULL}; // 串口接收回调登记表

// 私有工具：将硬件实例地址映射为数组索引
static int8_t _get_uart_index(USART_TypeDef *instance)
{
    int8_t idx = -1;

    // 使用 switch 替换 if-else，提高可读性。需强转为 uint32 进行地址匹配
    switch ((uint32_t)instance)
    {
    case (uint32_t)USART1: idx = 0; break;
    case (uint32_t)USART2: idx = 1; break;
    case (uint32_t)USART3: idx = 2; break;
    case (uint32_t)UART4:  idx = 3; break;
    case (uint32_t)UART5:  idx = 4; break;
    case (uint32_t)USART6: idx = 5; break;
    default:               idx = -1; break;
    }

    return idx;
}


/* ========================== [ 2. 阻塞式通信接口 ] ========================== */

void drv_uart_send_byte_blocking(UART_HandleTypeDef *huart, uint8_t data)
{
    drv_uart_send_buffer_blocking(huart, &data, 1U, 10U);
}

void drv_uart_send_string_blocking(UART_HandleTypeDef *huart, const char *str, uint32_t timeout_ms)
{
    if ((huart != NULL) && (str != NULL))
    {
        uint16_t len = (uint16_t)strlen(str);
        drv_uart_send_buffer_blocking(huart, (uint8_t *)str, len, timeout_ms);
    }
}

void drv_uart_send_buffer_blocking(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    if ((huart != NULL) && (buf != NULL) && (len > 0U))
    {
        /* 仅在硬件 Ready 时执行，确保不冲突 */
        if (huart->gState == HAL_UART_STATE_READY)
        {
            (void)HAL_UART_Transmit(huart, (uint8_t *)buf, len, timeout_ms);
        }
    }
}

bool drv_uart_read_byte_blocking(UART_HandleTypeDef *huart, uint8_t *out, uint32_t timeout_ms)
{
    bool result = false;

    if ((huart != NULL) && (out != NULL))
    {
        if (HAL_UART_Receive(huart, out, 1U, timeout_ms) == HAL_OK)
        {
            result = true;
        }
    }

    return result;
}


/* ========================== [ 3. 异步 DMA 发送 (TX) ] ========================== */

bool drv_uart_is_tx_free(UART_HandleTypeDef *huart)
{
    bool is_free = false;

    if (huart != NULL)
    {
        /* 检查 HAL 库内部发送状态机 */
        if (huart->gState == HAL_UART_STATE_READY)
        {
            is_free = true;
        }
    }

    return is_free;
}

bool drv_uart_send_dma(UART_HandleTypeDef *huart, const uint8_t *buf, uint16_t len)
{
    bool result = false;

    if ((huart != NULL) && (buf != NULL) && (len != 0U))
    {
        /* 硬件空闲时才启动 DMA */
        if (huart->gState == HAL_UART_STATE_READY)
        {
            if (HAL_UART_Transmit_DMA(huart, (uint8_t *)buf, len) == HAL_OK)
            {
                result = true;
            }
        }
    }

    return result;
}


/* ========================== [ 4. 异步 DMA 接收 (RX) ] ========================== */

bool drv_uart_receive_dma_start(UART_HandleTypeDef *huart, uint8_t *p_buf, uint16_t max_len)
{
    bool result = false;

    if ((huart != NULL) && (p_buf != NULL) && (max_len > 0U))
    {
        /* 开启 DMA 接收并监听 IDLE (空闲总线) 中断 */
        if (HAL_UARTEx_ReceiveToIdle_DMA(huart, p_buf, max_len) == HAL_OK)
        {
            result = true;
        }
    }

    return result;
}

void drv_uart_receive_dma_stop(UART_HandleTypeDef *huart)
{
    if (huart != NULL)
    {
        (void)HAL_UART_AbortReceive(huart);
    }
}


/* ========================== [ 5. 回调注册与分发中心 ] ========================== */

bool drv_uart_register_callback(UART_HandleTypeDef *huart, drv_uart_rx_callback_t callback)
{
    bool result = false;

    if (huart != NULL)
    {
        int8_t idx = _get_uart_index(huart->Instance);
        if (idx != -1)
        {
            s_rx_callbacks[idx] = callback;
            result = true;
        }
    }

    return result;
}

/**
 * @brief  HAL 库串口空闲中断回调
 * @param  huart 发生事件的串口句柄
 * @param  Size  本次实际接收到的字节数
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart != NULL)
    {
        int8_t idx = _get_uart_index(huart->Instance);

        /* 根据登记表自动分流，不再手动写 if-else */
        if ((idx != -1) && (s_rx_callbacks[idx] != NULL))
        {
            s_rx_callbacks[idx](Size);
        }
    }
}
