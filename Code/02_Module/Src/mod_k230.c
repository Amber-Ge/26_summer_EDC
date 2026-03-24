/**
 * @file    mod_k230.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   K230 协议模块实现（统一 UART 驱动回调版）。
 * @details
 * 1. 文件作用：实现 K230 协议接收解析、发送、绑定管理和运行态缓存。
 * 2. 解耦边界：协议层只关注帧格式与数据缓存，不管理任务调度与运动决策。
 * 3. 串口模型：通过 `drv_uart_register_rx_callback(..., user_ctx)` 绑定上下文。
 * 4. 资源仲裁：通过 `mod_uart_guard` 申请与释放 UART 所有权。
 */

#include "mod_k230.h"
#include "mod_uart_guard.h"
#include "common_checksum.h"
#include <string.h>

/* ========================== [ 1. 协议常量定义 ] ========================== */

/* 固定协议：AA AA 06 xx xx xx xx xx xx ck 55 55 */
#define K230_PROTO_HEADER_1               (0xAAU)
#define K230_PROTO_HEADER_2               (0xAAU)
#define K230_PROTO_LEN_FIXED              (0x06U)
#define K230_PROTO_CHECKSUM_START_IDX     (2U) /* [2]..[8] 参与 XOR */
#define K230_PROTO_CHECKSUM_LEN           (7U)
#define K230_PROTO_MOTOR1_ID_IDX          (3U)
#define K230_PROTO_ERR1_H_IDX             (4U)
#define K230_PROTO_ERR1_L_IDX             (5U)
#define K230_PROTO_MOTOR2_ID_IDX          (6U)
#define K230_PROTO_ERR2_H_IDX             (7U)
#define K230_PROTO_ERR2_L_IDX             (8U)
#define K230_PROTO_CHECKSUM_IDX           (9U)
#define K230_PROTO_TAIL_1_IDX             (10U)
#define K230_PROTO_TAIL_2_IDX             (11U)
#define K230_PROTO_TAIL_1                 (0x55U)
#define K230_PROTO_TAIL_2                 (0x55U)

/* ========================== [ 2. 静态资源 ] ========================== */

static mod_k230_ctx_t s_default_ctx; // 默认上下文实例

/* ========================== [ 3. 基础工具函数 ] ========================== */

static uint32_t _critical_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void _critical_exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static bool _checksum_algo_supported(mod_k230_checksum_algo_t algo)
{
    return (algo == MOD_K230_CHECKSUM_XOR);
}

static bool _ctx_ready(const mod_k230_ctx_t *ctx)
{
    return ((ctx != NULL) &&
            ctx->inited &&
            ctx->bound &&
            (ctx->bind.huart != NULL));
}

static void _reset_parser_state(mod_k230_ctx_t *ctx)
{
    ctx->parse_len = 0U;
    memset(ctx->parse_buf, 0, sizeof(ctx->parse_buf));
}

static void _reset_runtime_state(mod_k230_ctx_t *ctx)
{
    memset(ctx->rx_dma_buf, 0, sizeof(ctx->rx_dma_buf));
    memset(ctx->rx_ring_buf, 0, sizeof(ctx->rx_ring_buf));
    memset(ctx->tx_buf, 0, sizeof(ctx->tx_buf));
    ctx->rx_head = 0U;
    ctx->rx_tail = 0U;
    _reset_parser_state(ctx);
}

static bool _sem_exists(const mod_k230_bind_t *bind, osSemaphoreId_t sem_id)
{
    for (uint8_t i = 0U; i < bind->sem_count; i++)
    {
        if (bind->sem_list[i] == sem_id)
        {
            return true;
        }
    }

    return false;
}

static void _copy_bind_cfg(mod_k230_ctx_t *ctx, const mod_k230_bind_t *bind)
{
    ctx->bind.huart = bind->huart;
    ctx->bind.tx_mutex = bind->tx_mutex;
    ctx->bind.checksum_algo = bind->checksum_algo;
    ctx->bind.sem_count = 0U;
    memset(ctx->bind.sem_list, 0, sizeof(ctx->bind.sem_list));

    for (uint8_t i = 0U; (i < bind->sem_count) && (i < MOD_K230_MAX_BIND_SEM); i++)
    {
        osSemaphoreId_t sem_id = bind->sem_list[i];
        if ((sem_id != NULL) && !_sem_exists(&ctx->bind, sem_id))
        {
            ctx->bind.sem_list[ctx->bind.sem_count] = sem_id;
            ctx->bind.sem_count++;
        }
    }
}

static bool _bind_cfg_is_valid(const mod_k230_bind_t *bind)
{
    if ((bind == NULL) || (bind->huart == NULL))
    {
        return false;
    }

    if (drv_uart_get_port_index(bind->huart->Instance) < 0)
    {
        return false;
    }

    if (bind->sem_count > MOD_K230_MAX_BIND_SEM)
    {
        return false;
    }

    if (!_checksum_algo_supported(bind->checksum_algo))
    {
        return false;
    }

    return true;
}

/* ========================== [ 4. 环形缓冲工具 ] ========================== */

static uint16_t _ring_count(const mod_k230_ctx_t *ctx)
{
    uint16_t count;

    if (ctx->rx_head >= ctx->rx_tail)
    {
        count = (uint16_t)(ctx->rx_head - ctx->rx_tail);
    }
    else
    {
        count = (uint16_t)(MOD_K230_RX_RING_BUF_SIZE - ctx->rx_tail + ctx->rx_head);
    }

    return count;
}

static void _ring_push_byte(mod_k230_ctx_t *ctx, uint8_t data)
{
    uint16_t next_head = (uint16_t)(ctx->rx_head + 1U);

    if (next_head >= MOD_K230_RX_RING_BUF_SIZE)
    {
        next_head = 0U;
    }

    if (next_head == ctx->rx_tail)
    {
        ctx->rx_tail = (uint16_t)(ctx->rx_tail + 1U);
        if (ctx->rx_tail >= MOD_K230_RX_RING_BUF_SIZE)
        {
            ctx->rx_tail = 0U;
        }
    }

    ctx->rx_ring_buf[ctx->rx_head] = data;
    ctx->rx_head = next_head;
}

static void _ring_push_block(mod_k230_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (data == NULL)
    {
        return;
    }

    for (uint16_t i = 0U; i < len; i++)
    {
        _ring_push_byte(ctx, data[i]);
    }
}

/* ========================== [ 5. 协议解析工具 ] ========================== */

static bool _proto_frame_is_valid(const mod_k230_ctx_t *ctx, const uint8_t *frame)
{
    if ((ctx == NULL) || (frame == NULL))
    {
        return false;
    }

    if ((frame[0] != K230_PROTO_HEADER_1) ||
        (frame[1] != K230_PROTO_HEADER_2) ||
        (frame[2] != K230_PROTO_LEN_FIXED) ||
        (frame[K230_PROTO_TAIL_1_IDX] != K230_PROTO_TAIL_1) ||
        (frame[K230_PROTO_TAIL_2_IDX] != K230_PROTO_TAIL_2))
    {
        return false;
    }

    switch (ctx->bind.checksum_algo)
    {
    case MOD_K230_CHECKSUM_XOR:
    {
        uint8_t checksum = common_checksum_xor_u8(&frame[K230_PROTO_CHECKSUM_START_IDX],
                                                  K230_PROTO_CHECKSUM_LEN);
        return (checksum == frame[K230_PROTO_CHECKSUM_IDX]);
    }
    default:
        return false;
    }
}

static void _proto_decode_frame(const uint8_t *frame, mod_k230_frame_data_t *out_frame)
{
    if ((frame == NULL) || (out_frame == NULL))
    {
        return;
    }

    out_frame->motor1_id = frame[K230_PROTO_MOTOR1_ID_IDX];
    out_frame->err1 = (int16_t)(((uint16_t)frame[K230_PROTO_ERR1_H_IDX] << 8U) |
                                (uint16_t)frame[K230_PROTO_ERR1_L_IDX]);
    out_frame->motor2_id = frame[K230_PROTO_MOTOR2_ID_IDX];
    out_frame->err2 = (int16_t)(((uint16_t)frame[K230_PROTO_ERR2_H_IDX] << 8U) |
                                (uint16_t)frame[K230_PROTO_ERR2_L_IDX]);
}

static void _proto_resync_after_invalid(mod_k230_ctx_t *ctx)
{
    uint8_t new_len = 0U;

    for (uint8_t i = 1U; i < MOD_K230_PROTO_FRAME_SIZE; i++)
    {
        if (ctx->parse_buf[i] == K230_PROTO_HEADER_1)
        {
            if (i == (MOD_K230_PROTO_FRAME_SIZE - 1U))
            {
                ctx->parse_buf[0] = K230_PROTO_HEADER_1;
                new_len = 1U;
                break;
            }

            if (ctx->parse_buf[i + 1U] == K230_PROTO_HEADER_2)
            {
                new_len = (uint8_t)(MOD_K230_PROTO_FRAME_SIZE - i);
                memcpy(ctx->parse_buf, &ctx->parse_buf[i], new_len);
                break;
            }
        }
    }

    ctx->parse_len = new_len;
}

static bool _proto_feed_byte(mod_k230_ctx_t *ctx, uint8_t byte, mod_k230_frame_data_t *out_frame)
{
    bool got_frame = false;

    if (ctx->parse_len == 0U)
    {
        if (byte == K230_PROTO_HEADER_1)
        {
            ctx->parse_buf[0] = byte;
            ctx->parse_len = 1U;
        }
        return false;
    }

    if (ctx->parse_len == 1U)
    {
        if (byte == K230_PROTO_HEADER_2)
        {
            ctx->parse_buf[1] = byte;
            ctx->parse_len = 2U;
        }
        else if (byte == K230_PROTO_HEADER_1)
        {
            ctx->parse_buf[0] = K230_PROTO_HEADER_1;
            ctx->parse_len = 1U;
        }
        else
        {
            ctx->parse_len = 0U;
        }
        return false;
    }

    ctx->parse_buf[ctx->parse_len] = byte;
    ctx->parse_len++;

    if (ctx->parse_len >= MOD_K230_PROTO_FRAME_SIZE)
    {
        if (_proto_frame_is_valid(ctx, ctx->parse_buf))
        {
            _proto_decode_frame(ctx->parse_buf, out_frame);
            got_frame = true;
            ctx->parse_len = 0U;
        }
        else
        {
            _proto_resync_after_invalid(ctx);
        }
    }

    return got_frame;
}

/* ========================== [ 6. 收发辅助工具 ] ========================== */

static bool _tx_lock(mod_k230_ctx_t *ctx)
{
    if (ctx->bind.tx_mutex == NULL)
    {
        return true;
    }

    return (osMutexAcquire(ctx->bind.tx_mutex, MOD_K230_TX_MUTEX_TIMEOUT_MS) == osOK);
}

static void _tx_unlock(mod_k230_ctx_t *ctx)
{
    if (ctx->bind.tx_mutex != NULL)
    {
        (void)osMutexRelease(ctx->bind.tx_mutex);
    }
}

static void _notify_all_bound_semaphores(const mod_k230_ctx_t *ctx)
{
    for (uint8_t i = 0U; i < ctx->bind.sem_count; i++)
    {
        if (ctx->bind.sem_list[i] != NULL)
        {
            (void)osSemaphoreRelease(ctx->bind.sem_list[i]);
        }
    }
}

static void _restart_rx_dma(mod_k230_ctx_t *ctx)
{
    drv_uart_status_t status;

    if (!_ctx_ready(ctx))
    {
        return;
    }

    status = drv_uart_receive_dma_restart_ex(ctx->bind.huart, ctx->rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE);
    if (status != DRV_UART_OK)
    {
        status = drv_uart_receive_dma_start_ex(ctx->bind.huart, ctx->rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE);
    }

    if (status == DRV_UART_OK)
    {
        (void)drv_uart_disable_rx_dma_half_transfer_irq(ctx->bind.huart);
    }
}

static void _k230_rx_callback_handler(UART_HandleTypeDef *huart,
                                      drv_uart_rx_event_t event,
                                      const uint8_t *data,
                                      uint16_t len,
                                      void *user_ctx)
{
    mod_k230_ctx_t *ctx = (mod_k230_ctx_t *)user_ctx;

    if (!_ctx_ready(ctx))
    {
        return;
    }

    if (ctx->bind.huart != huart)
    {
        return;
    }

    /* HT 仅代表半缓冲完成，此处忽略，等待 IDLE/TC 再搬运完整数据块。 */
    if (event == DRV_UART_RX_EVENT_HT)
    {
        return;
    }

    if (len > MOD_K230_RX_DMA_BUF_SIZE)
    {
        len = MOD_K230_RX_DMA_BUF_SIZE;
    }

    if ((data != NULL) && (data != ctx->rx_dma_buf) && (len > 0U))
    {
        memcpy(ctx->rx_dma_buf, data, len);
    }

    if (len > 0U)
    {
        _ring_push_block(ctx, ctx->rx_dma_buf, len);
        _notify_all_bound_semaphores(ctx);
    }

    _restart_rx_dma(ctx);
}

/* ========================== [ 7. Public API ] ========================== */

mod_k230_ctx_t *mod_k230_get_default_ctx(void)
{
    return &s_default_ctx;
}

bool mod_k230_ctx_init(mod_k230_ctx_t *ctx, const mod_k230_bind_t *bind)
{
    if (ctx == NULL)
    {
        return false;
    }

    if (ctx->inited && ctx->bound)
    {
        mod_k230_unbind(ctx);
    }

    memset(ctx, 0, sizeof(mod_k230_ctx_t));
    ctx->inited = true;
    ctx->bind.checksum_algo = MOD_K230_CHECKSUM_XOR;

    if (bind != NULL)
    {
        return mod_k230_bind(ctx, bind);
    }

    return true;
}

void mod_k230_ctx_deinit(mod_k230_ctx_t *ctx)
{
    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    mod_k230_unbind(ctx);
    memset(ctx, 0, sizeof(mod_k230_ctx_t));
}

bool mod_k230_bind(mod_k230_ctx_t *ctx, const mod_k230_bind_t *bind)
{
    drv_uart_status_t drv_status;

    if ((ctx == NULL) || !ctx->inited || !_bind_cfg_is_valid(bind))
    {
        return false;
    }

    if (ctx->bound)
    {
        mod_k230_unbind(ctx);
    }

    if (!mod_uart_guard_claim_ctx(bind->huart, MOD_UART_OWNER_K230, ctx))
    {
        return false;
    }

    _copy_bind_cfg(ctx, bind);
    _reset_runtime_state(ctx);

    drv_status = drv_uart_register_rx_callback(ctx->bind.huart, _k230_rx_callback_handler, ctx);
    if (drv_status == DRV_UART_OK)
    {
        drv_status = drv_uart_receive_dma_start_ex(ctx->bind.huart, ctx->rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE);
    }
    if (drv_status == DRV_UART_OK)
    {
        (void)drv_uart_disable_rx_dma_half_transfer_irq(ctx->bind.huart);
    }

    if (drv_status != DRV_UART_OK)
    {
        (void)drv_uart_unregister_rx_callback(ctx->bind.huart);
        (void)mod_uart_guard_release_ctx(ctx->bind.huart, MOD_UART_OWNER_K230, ctx);
        memset(&ctx->bind, 0, sizeof(ctx->bind));
        ctx->bind.checksum_algo = MOD_K230_CHECKSUM_XOR;
        ctx->bound = false;
        _reset_runtime_state(ctx);
        return false;
    }

    ctx->bound = true;
    return true;
}

void mod_k230_unbind(mod_k230_ctx_t *ctx)
{
    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    if (ctx->bound && (ctx->bind.huart != NULL))
    {
        (void)drv_uart_receive_dma_stop_ex(ctx->bind.huart);
        (void)drv_uart_unregister_rx_callback(ctx->bind.huart);
        (void)mod_uart_guard_release_ctx(ctx->bind.huart, MOD_UART_OWNER_K230, ctx);
    }

    memset(&ctx->bind, 0, sizeof(ctx->bind));
    ctx->bind.checksum_algo = MOD_K230_CHECKSUM_XOR;
    ctx->bound = false;
    _reset_runtime_state(ctx);
}

bool mod_k230_is_bound(const mod_k230_ctx_t *ctx)
{
    return _ctx_ready(ctx);
}

bool mod_k230_set_checksum_algo(mod_k230_ctx_t *ctx, mod_k230_checksum_algo_t algo)
{
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited || !_checksum_algo_supported(algo))
    {
        return false;
    }

    primask = _critical_enter();
    ctx->bind.checksum_algo = algo;
    _critical_exit(primask);
    return true;
}

bool mod_k230_add_semaphore(mod_k230_ctx_t *ctx, osSemaphoreId_t sem_id)
{
    bool result = false;
    uint32_t primask;

    if (!_ctx_ready(ctx) || (sem_id == NULL))
    {
        return false;
    }

    primask = _critical_enter();

    if (_sem_exists(&ctx->bind, sem_id))
    {
        result = true;
    }
    else if (ctx->bind.sem_count < MOD_K230_MAX_BIND_SEM)
    {
        ctx->bind.sem_list[ctx->bind.sem_count] = sem_id;
        ctx->bind.sem_count++;
        result = true;
    }

    _critical_exit(primask);
    return result;
}

bool mod_k230_remove_semaphore(mod_k230_ctx_t *ctx, osSemaphoreId_t sem_id)
{
    bool result = false;
    uint32_t primask;

    if (!_ctx_ready(ctx) || (sem_id == NULL))
    {
        return false;
    }

    primask = _critical_enter();

    for (uint8_t i = 0U; i < ctx->bind.sem_count; i++)
    {
        if (ctx->bind.sem_list[i] == sem_id)
        {
            for (uint8_t j = i; (j + 1U) < ctx->bind.sem_count; j++)
            {
                ctx->bind.sem_list[j] = ctx->bind.sem_list[j + 1U];
            }
            ctx->bind.sem_count--;
            ctx->bind.sem_list[ctx->bind.sem_count] = NULL;
            result = true;
            break;
        }
    }

    _critical_exit(primask);
    return result;
}

void mod_k230_clear_semaphores(mod_k230_ctx_t *ctx)
{
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    primask = _critical_enter();
    memset(ctx->bind.sem_list, 0, sizeof(ctx->bind.sem_list));
    ctx->bind.sem_count = 0U;
    _critical_exit(primask);
}

void mod_k230_set_tx_mutex(mod_k230_ctx_t *ctx, osMutexId_t mutex_id)
{
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    primask = _critical_enter();
    ctx->bind.tx_mutex = mutex_id;
    _critical_exit(primask);
}

bool mod_k230_send_bytes(mod_k230_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    bool result = false;

    if (!_ctx_ready(ctx) || (data == NULL) || (len == 0U) || (len > MOD_K230_TX_BUF_SIZE))
    {
        return false;
    }

    if (!_tx_lock(ctx))
    {
        return false;
    }

    if (drv_uart_is_tx_free(ctx->bind.huart))
    {
        memcpy(ctx->tx_buf, data, len);
        result = (drv_uart_send_dma_ex(ctx->bind.huart, ctx->tx_buf, len) == DRV_UART_OK);
    }

    _tx_unlock(ctx);
    return result;
}

bool mod_k230_is_tx_free(const mod_k230_ctx_t *ctx)
{
    if (!_ctx_ready(ctx))
    {
        return false;
    }

    return drv_uart_is_tx_free(ctx->bind.huart);
}

uint16_t mod_k230_available(const mod_k230_ctx_t *ctx)
{
    uint16_t count;
    uint32_t primask;

    if (!_ctx_ready(ctx))
    {
        return 0U;
    }

    primask = _critical_enter();
    count = _ring_count(ctx);
    _critical_exit(primask);
    return count;
}

uint16_t mod_k230_read_bytes(mod_k230_ctx_t *ctx, uint8_t *out, uint16_t max_len)
{
    uint16_t read_len = 0U;
    uint32_t primask;

    if (!_ctx_ready(ctx) || (out == NULL) || (max_len == 0U))
    {
        return 0U;
    }

    primask = _critical_enter();

    while ((read_len < max_len) && (ctx->rx_tail != ctx->rx_head))
    {
        out[read_len] = ctx->rx_ring_buf[ctx->rx_tail];
        read_len++;

        ctx->rx_tail = (uint16_t)(ctx->rx_tail + 1U);
        if (ctx->rx_tail >= MOD_K230_RX_RING_BUF_SIZE)
        {
            ctx->rx_tail = 0U;
        }
    }

    _critical_exit(primask);
    return read_len;
}

void mod_k230_clear_rx_buffer(mod_k230_ctx_t *ctx)
{
    uint32_t primask;

    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    primask = _critical_enter();
    ctx->rx_head = 0U;
    ctx->rx_tail = 0U;
    memset(ctx->rx_ring_buf, 0, sizeof(ctx->rx_ring_buf));
    _reset_parser_state(ctx);
    _critical_exit(primask);
}

bool mod_k230_get_latest_frame(mod_k230_ctx_t *ctx, mod_k230_frame_data_t *out_frame)
{
    bool got_latest = false;
    uint8_t read_buf[64];
    uint16_t read_len;
    uint8_t batch_count = 0U;
    mod_k230_frame_data_t latest_frame = {0};

    if (!_ctx_ready(ctx) || (out_frame == NULL))
    {
        return false;
    }

    do
    {
        if (batch_count >= MOD_K230_MAX_READ_BATCH_PER_CALL)
        {
            break;
        }

        read_len = mod_k230_read_bytes(ctx, read_buf, (uint16_t)sizeof(read_buf));
        batch_count++;

        for (uint16_t i = 0U; i < read_len; i++)
        {
            mod_k230_frame_data_t parsed_frame;
            if (_proto_feed_byte(ctx, read_buf[i], &parsed_frame))
            {
                latest_frame = parsed_frame;
                got_latest = true;
            }
        }
    } while (read_len > 0U);

    if (got_latest)
    {
        *out_frame = latest_frame;
    }

    return got_latest;
}

