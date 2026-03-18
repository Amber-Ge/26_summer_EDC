#include "mod_k230.h"
#include "mod_uart_guard.h"
#include "common_checksum.h"
#include <string.h>

/**
 * @file mod_k230.c
 * @brief K230 协议层模块实现（解耦版）
 * @details
 * 关键实现点：
 * 1. 使用 ctx 承载全部运行态，避免旧版“全局静态单实例”耦合。
 * 2. 使用 bind 承载全部硬件/OS 资源注入信息（UART、mutex、semaphore）。
 * 3. 校验算法由 bind.checksum_algo 决定，当前实现 XOR。
 * 4. 通过 UART->ctx 映射和固定回调分发表，支持“每个 UART 对应一个 K230 ctx”。
 */

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

/* 与 drv_uart/mod_uart_guard 内部索引规模保持一致：USART1/2/3/UART4/5/USART6 */
#define MOD_K230_MAX_UART_INSTANCES       (6U)

/* ========================== [ 2. 静态资源 ] ========================== */

/* 默认上下文，供外部在不自建实例时直接使用。 */
static mod_k230_ctx_t s_default_ctx;

/* UART 实例到 K230 ctx 的反向映射，用于 RX 中断事件分发。 */
static mod_k230_ctx_t *s_ctx_by_uart[MOD_K230_MAX_UART_INSTANCES] = {NULL};

/* ========================== [ 3. 基础工具函数 ] ========================== */

/**
 * @brief 将 UART 外设实例映射到固定索引。
 * @param instance UART 实例地址。
 * @return int8_t 成功返回 [0,5]，失败返回 -1。
 */
static int8_t _get_uart_index(USART_TypeDef *instance)
{
    int8_t idx = -1;

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

/**
 * @brief 进入临界区（关中断）。
 * @return uint32_t 进入前 PRIMASK。
 */
static uint32_t _critical_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/**
 * @brief 退出临界区（恢复中断状态）。
 * @param primask 进入临界区前保存的 PRIMASK。
 */
static void _critical_exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/**
 * @brief 判断校验算法是否已实现。
 * @param algo 目标算法。
 * @return true 支持。
 * @return false 不支持。
 */
static bool _checksum_algo_supported(mod_k230_checksum_algo_t algo)
{
    return (algo == MOD_K230_CHECKSUM_XOR);
}

/**
 * @brief 判断上下文是否达到可工作状态。
 * @param ctx 上下文指针。
 * @return true 可工作。
 * @return false 不可工作。
 */
static bool _ctx_ready(const mod_k230_ctx_t *ctx)
{
    return ((ctx != NULL) &&
            ctx->inited &&
            ctx->bound &&
            (ctx->bind.huart != NULL));
}

/**
 * @brief 重置协议解析状态机。
 * @param ctx 上下文指针。
 */
static void _reset_parser_state(mod_k230_ctx_t *ctx)
{
    ctx->parse_len = 0U;
    memset(ctx->parse_buf, 0, sizeof(ctx->parse_buf));
}

/**
 * @brief 重置运行态缓存（不修改 inited/bound，不释放 UART 资源）。
 * @param ctx 上下文指针。
 */
static void _reset_runtime_state(mod_k230_ctx_t *ctx)
{
    memset(ctx->rx_dma_buf, 0, sizeof(ctx->rx_dma_buf));
    memset(ctx->rx_ring_buf, 0, sizeof(ctx->rx_ring_buf));
    memset(ctx->tx_buf, 0, sizeof(ctx->tx_buf));
    ctx->rx_head = 0U;
    ctx->rx_tail = 0U;
    _reset_parser_state(ctx);
}

/**
 * @brief 判断 sem 是否已存在于绑定列表中。
 * @param bind 绑定结构。
 * @param sem_id 目标信号量。
 * @return true 已存在。
 * @return false 不存在。
 */
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

/**
 * @brief 将外部 bind 配置复制到 ctx->bind，并做信号量列表清洗。
 * @details
 * 清洗规则：
 * 1. 忽略 NULL 信号量；
 * 2. 自动去重；
 * 3. 最多保留 MOD_K230_MAX_BIND_SEM 个。
 *
 * @param ctx 目标上下文。
 * @param bind 外部绑定配置。
 */
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

/**
 * @brief 校验 bind 参数完整性。
 * @param bind 外部绑定配置。
 * @return true 合法。
 * @return false 非法。
 */
static bool _bind_cfg_is_valid(const mod_k230_bind_t *bind)
{
    if ((bind == NULL) || (bind->huart == NULL))
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

/**
 * @brief 获取环形缓冲当前有效字节数。
 * @param ctx 上下文。
 * @return uint16_t 可读字节数。
 */
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

/**
 * @brief 向环形缓冲写入一个字节。
 * @details
 * 当缓冲满时采用“覆盖最旧数据”策略，保证最新数据尽量保留。
 *
 * @param ctx 上下文。
 * @param data 字节数据。
 */
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

/**
 * @brief 批量写入环形缓冲。
 * @param ctx 上下文。
 * @param data 数据地址。
 * @param len 数据长度。
 */
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

/**
 * @brief 按当前绑定算法校验完整帧合法性。
 * @param ctx 上下文。
 * @param frame 帧缓冲（长度固定 MOD_K230_PROTO_FRAME_SIZE）。
 * @return true 帧有效。
 * @return false 帧无效。
 */
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
        /* 预留分支：未来扩展新算法后在此追加。 */
        return false;
    }
}

/**
 * @brief 将完整有效帧解码为业务数据结构。
 * @param frame 输入帧。
 * @param out_frame 输出结构体。
 */
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

/**
 * @brief 当解析到无效帧时，尝试在当前缓存中重同步帧头。
 * @details
 * 策略：
 * 1. 从 parse_buf[1] 开始扫描潜在帧头 AA AA；
 * 2. 若找到则将剩余字节前移，保留为下次继续拼帧的前缀；
 * 3. 若仅最后一个字节是 AA，则保留 1 字节前缀；
 * 4. 否则解析缓存清空。
 *
 * @param ctx 上下文。
 */
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

/**
 * @brief 向流式解析器喂入 1 字节。
 * @param ctx 上下文。
 * @param byte 输入字节。
 * @param out_frame 若本次完成一帧有效解码，则输出结果。
 * @return true 本次喂入后得到一帧有效帧。
 * @return false 未得到有效帧。
 */
static bool _proto_feed_byte(mod_k230_ctx_t *ctx, uint8_t byte, mod_k230_frame_data_t *out_frame)
{
    bool got_frame = false;

    /* 状态0：仅接受第一个帧头字节 AA。 */
    if (ctx->parse_len == 0U)
    {
        if (byte == K230_PROTO_HEADER_1)
        {
            ctx->parse_buf[0] = byte;
            ctx->parse_len = 1U;
        }
        return false;
    }

    /* 状态1：仅接受第二个帧头字节 AA。 */
    if (ctx->parse_len == 1U)
    {
        if (byte == K230_PROTO_HEADER_2)
        {
            ctx->parse_buf[1] = byte;
            ctx->parse_len = 2U;
        }
        else if (byte == K230_PROTO_HEADER_1)
        {
            /* 连续 AA 的场景：保持为“已收到第1个头字节”。 */
            ctx->parse_buf[0] = K230_PROTO_HEADER_1;
            ctx->parse_len = 1U;
        }
        else
        {
            ctx->parse_len = 0U;
        }
        return false;
    }

    /* 状态2..N：持续收集直到凑满固定帧长。 */
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

/**
 * @brief 获取发送互斥锁。
 * @param ctx 上下文。
 * @return true 加锁成功或未配置互斥锁。
 * @return false 加锁失败。
 */
static bool _tx_lock(mod_k230_ctx_t *ctx)
{
    if (ctx->bind.tx_mutex == NULL)
    {
        return true;
    }
    return (osMutexAcquire(ctx->bind.tx_mutex, MOD_K230_TX_MUTEX_TIMEOUT_MS) == osOK);
}

/**
 * @brief 释放发送互斥锁。
 * @param ctx 上下文。
 */
static void _tx_unlock(mod_k230_ctx_t *ctx)
{
    if (ctx->bind.tx_mutex != NULL)
    {
        (void)osMutexRelease(ctx->bind.tx_mutex);
    }
}

/**
 * @brief 通知全部已绑定信号量“有新接收数据”。
 * @param ctx 上下文。
 */
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

/**
 * @brief 重启 DMA 接收；失败时做一次 stop+retry。
 * @param ctx 上下文。
 */
static void _restart_rx_dma(mod_k230_ctx_t *ctx)
{
    if (!_ctx_ready(ctx))
    {
        return;
    }

    if (!drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE))
    {
        drv_uart_receive_dma_stop(ctx->bind.huart);
        (void)drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE);
    }
}

/**
 * @brief K230 专属 RX 事件处理（由分发器携带 ctx 调用）。
 * @param ctx 上下文。
 * @param len 本次 DMA 回调报告长度。
 */
static void _k230_rx_callback_handler(mod_k230_ctx_t *ctx, uint16_t len)
{
    if (!_ctx_ready(ctx))
    {
        return;
    }

    if (len > MOD_K230_RX_DMA_BUF_SIZE)
    {
        len = MOD_K230_RX_DMA_BUF_SIZE;
    }

    if (len > 0U)
    {
        _ring_push_block(ctx, ctx->rx_dma_buf, len);
        _notify_all_bound_semaphores(ctx);
    }

    _restart_rx_dma(ctx);
}

/**
 * @brief UART 索引分发入口。
 * @param uart_idx UART 固定索引。
 * @param len 本次回调长度。
 */
static void _k230_rx_dispatch(uint8_t uart_idx, uint16_t len)
{
    if (uart_idx < MOD_K230_MAX_UART_INSTANCES)
    {
        mod_k230_ctx_t *ctx = s_ctx_by_uart[uart_idx];
        if (ctx != NULL)
        {
            _k230_rx_callback_handler(ctx, len);
        }
    }
}

static void _k230_rx_cb_0(uint16_t len) { _k230_rx_dispatch(0U, len); }
static void _k230_rx_cb_1(uint16_t len) { _k230_rx_dispatch(1U, len); }
static void _k230_rx_cb_2(uint16_t len) { _k230_rx_dispatch(2U, len); }
static void _k230_rx_cb_3(uint16_t len) { _k230_rx_dispatch(3U, len); }
static void _k230_rx_cb_4(uint16_t len) { _k230_rx_dispatch(4U, len); }
static void _k230_rx_cb_5(uint16_t len) { _k230_rx_dispatch(5U, len); }

/* 固定回调表：按 UART 索引选对应分发入口。 */
static drv_uart_rx_callback_t s_k230_rx_cbs[MOD_K230_MAX_UART_INSTANCES] =
{
    _k230_rx_cb_0,
    _k230_rx_cb_1,
    _k230_rx_cb_2,
    _k230_rx_cb_3,
    _k230_rx_cb_4,
    _k230_rx_cb_5
};

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
    int8_t uart_idx;
    bool result;

    if ((ctx == NULL) || !ctx->inited || !_bind_cfg_is_valid(bind))
    {
        return false;
    }

    uart_idx = _get_uart_index(bind->huart->Instance);
    if (uart_idx < 0)
    {
        return false;
    }

    if ((s_ctx_by_uart[(uint8_t)uart_idx] != NULL) && (s_ctx_by_uart[(uint8_t)uart_idx] != ctx))
    {
        return false;
    }

    /* 重新绑定时，先完整释放旧绑定，确保 UART 归属和回调状态干净。 */
    if (ctx->bound)
    {
        mod_k230_unbind(ctx);
    }

    if (!mod_uart_guard_claim(bind->huart, MOD_UART_OWNER_K230))
    {
        return false;
    }

    _copy_bind_cfg(ctx, bind);
    _reset_runtime_state(ctx);

    /* 先登记映射，再注册回调并启动 DMA，避免“先到回调找不到 ctx”。 */
    s_ctx_by_uart[(uint8_t)uart_idx] = ctx;

    result = drv_uart_register_callback(ctx->bind.huart, s_k230_rx_cbs[(uint8_t)uart_idx]);
    if (result)
    {
        result = drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE);
    }

    if (!result)
    {
        (void)drv_uart_register_callback(ctx->bind.huart, NULL);
        s_ctx_by_uart[(uint8_t)uart_idx] = NULL;
        (void)mod_uart_guard_release(ctx->bind.huart, MOD_UART_OWNER_K230);
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
    int8_t uart_idx;

    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    if (ctx->bound && (ctx->bind.huart != NULL))
    {
        drv_uart_receive_dma_stop(ctx->bind.huart);
        (void)drv_uart_register_callback(ctx->bind.huart, NULL);
        (void)mod_uart_guard_release(ctx->bind.huart, MOD_UART_OWNER_K230);

        uart_idx = _get_uart_index(ctx->bind.huart->Instance);
        if ((uart_idx >= 0) && (s_ctx_by_uart[(uint8_t)uart_idx] == ctx))
        {
            s_ctx_by_uart[(uint8_t)uart_idx] = NULL;
        }
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
        result = drv_uart_send_dma(ctx->bind.huart, ctx->tx_buf, len);
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
    mod_k230_frame_data_t latest_frame = {0};

    if (!_ctx_ready(ctx) || (out_frame == NULL))
    {
        return false;
    }

    do
    {
        read_len = mod_k230_read_bytes(ctx, read_buf, (uint16_t)sizeof(read_buf));

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
