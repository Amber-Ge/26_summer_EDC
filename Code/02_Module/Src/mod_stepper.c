#include "mod_stepper.h"
#include "mod_uart_guard.h"

/**
 * @file mod_stepper.c
 * @brief 步进电机协议模块实现
 * @details
 * 负责步进驱动串口绑定、DMA 收发、收发队列管理、协议帧构建与状态查询。
 */

static mod_stepper_ctx_t *s_ctx_by_uart[MOD_STEPPER_MAX_UART_INSTANCES] = {NULL}; // 串口实例到上下文的映射表
static mod_stepper_ctx_t *s_ctx_by_logic[MOD_STEPPER_LOGIC_ID_MAX + 1U] = {NULL}; // 逻辑电机 ID 到上下文的映射表

/**
 * @brief 将 UART 实例地址映射为索引。
 * @param instance UART 外设实例。
 * @return int8_t 成功返回 [0,5]，失败返回 -1。
 */
static int8_t _get_uart_index(USART_TypeDef *instance)
{
    int8_t idx = -1; // UART 映射索引

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
    uint32_t primask = __get_PRIMASK(); // 进入前中断屏蔽状态
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

static bool _ctx_is_ready(const mod_stepper_ctx_t *ctx)
{
    //1. 判定上下文是否已初始化且已完成串口/地址绑定
    return ((ctx != NULL) &&
            ctx->inited &&
            (ctx->bind.huart != NULL) &&
            (ctx->bind.motor_id > 0U) &&
            (ctx->bind.driver_addr > 0U));
}

/**
 * @brief 查找指定上下文当前绑定的 UART 索引。
 * @param ctx 上下文指针。
 * @return int8_t 成功返回索引，失败返回 -1。
 */
static int8_t _ctx_bound_uart_idx(const mod_stepper_ctx_t *ctx)
{
    //1. 参数校验
    if (ctx == NULL)
    {
        return -1;
    }

    //2. 扫描映射表，匹配当前上下文
    for (uint16_t i = 0U; i < MOD_STEPPER_MAX_UART_INSTANCES; i++)
    {
        if (s_ctx_by_uart[i] == ctx)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

/**
 * @brief 根据上下文/地址解析只读上下文指针。
 * @param ctx 指定上下文，可为 NULL。
 * @param addr 目标地址（逻辑 ID 或驱动地址）。
 * @return const mod_stepper_ctx_t* 解析成功返回目标上下文，失败返回 NULL。
 */
static const mod_stepper_ctx_t *_resolve_ctx_const(const mod_stepper_ctx_t *ctx, uint8_t addr)
{
    const mod_stepper_ctx_t *mapped; // 映射结果缓存

    //1. 优先匹配传入 ctx 本身
    if ((ctx != NULL) && _ctx_is_ready(ctx))
    {
        if ((addr == ctx->bind.motor_id) || (addr == ctx->bind.driver_addr))
        {
            return ctx;
        }
    }

    //2. 按逻辑 ID 映射表查找
    if ((addr > 0U) && (addr <= MOD_STEPPER_LOGIC_ID_MAX))
    {
        mapped = s_ctx_by_logic[addr];
        if ((mapped != NULL) && _ctx_is_ready(mapped))
        {
            return mapped;
        }
    }

    //3. 若未指定 ctx，则按驱动地址在全部上下文中查找
    if (ctx == NULL)
    {
        for (uint16_t i = 0U; i < MOD_STEPPER_MAX_UART_INSTANCES; i++)
        {
            mapped = s_ctx_by_uart[i];
            if ((mapped != NULL) && _ctx_is_ready(mapped) && (mapped->bind.driver_addr == addr))
            {
                return mapped;
            }
        }
    }

    return NULL;
}

static mod_stepper_ctx_t *_resolve_ctx(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    //1. 复用 const 版本解析，再转回可写指针
    return (mod_stepper_ctx_t *)_resolve_ctx_const((const mod_stepper_ctx_t *)ctx, addr);
}

/**
 * @brief 复位发送队列状态。
 * @param ctx 步进上下文。
 */
static void _tx_queue_reset(mod_stepper_ctx_t *ctx)
{
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验
    if (ctx == NULL)
    {
        return;
    }

    //2. 临界区内清空发送队列读写索引与计数
    primask = _critical_enter();
    ctx->tx_q_head = 0U;
    ctx->tx_q_tail = 0U;
    ctx->tx_q_count = 0U;
    _critical_exit(primask);
}

/**
 * @brief 向发送队列压入一帧数据。
 * @param ctx 步进上下文。
 * @param buf 帧数据。
 * @param len 帧长度。
 * @return true 入队成功。
 * @return false 入队失败。
 */
static bool _tx_queue_push(mod_stepper_ctx_t *ctx, const uint8_t *buf, uint16_t len)
{
    bool result = false; // 入队结果
    uint32_t primask; // 临界区状态保存值

    //1. 参数与长度校验
    if ((ctx == NULL) || (buf == NULL) || (len == 0U) || (len > MOD_STEPPER_TX_BUF_SIZE))
    {
        return false;
    }

    //2. 临界区内执行入队或丢包计数
    primask = _critical_enter();

    if (ctx->tx_q_count < MOD_STEPPER_TX_QUEUE_SIZE)
    {
        ctx->tx_queue[ctx->tx_q_tail].len = len;
        memcpy(ctx->tx_queue[ctx->tx_q_tail].data, buf, len);
        ctx->tx_q_tail++;
        if (ctx->tx_q_tail >= MOD_STEPPER_TX_QUEUE_SIZE)
        {
            ctx->tx_q_tail = 0U;
        }
        ctx->tx_q_count++;
        ctx->tx_push_count++;
        result = true;
    }
    else
    {
        ctx->tx_drop_count++;
    }

    _critical_exit(primask);
    return result;
}

/**
 * @brief 查看发送队列队首帧（不出队）。
 * @param ctx 步进上下文。
 * @param out_buf 输出缓冲区。
 * @param out_len 输出长度。
 * @return true 获取成功。
 * @return false 获取失败。
 */
static bool _tx_queue_peek(mod_stepper_ctx_t *ctx, uint8_t *out_buf, uint16_t *out_len)
{
    bool result = false; // 获取结果
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验
    if ((ctx == NULL) || (out_buf == NULL) || (out_len == NULL))
    {
        return false;
    }

    //2. 临界区内读取队首数据，不改变队列状态
    primask = _critical_enter();

    if (ctx->tx_q_count > 0U)
    {
        uint16_t len = ctx->tx_queue[ctx->tx_q_head].len;
        memcpy(out_buf, ctx->tx_queue[ctx->tx_q_head].data, len);
        *out_len = len;
        result = true;
    }

    _critical_exit(primask);
    return result;
}

/**
 * @brief 弹出发送队列队首帧。
 * @param ctx 步进上下文。
 */
static void _tx_queue_pop(mod_stepper_ctx_t *ctx)
{
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验
    if (ctx == NULL)
    {
        return;
    }

    //2. 临界区内推进队首索引
    primask = _critical_enter();

    if (ctx->tx_q_count > 0U)
    {
        ctx->tx_q_head++;
        if (ctx->tx_q_head >= MOD_STEPPER_TX_QUEUE_SIZE)
        {
            ctx->tx_q_head = 0U;
        }
        ctx->tx_q_count--;
    }

    _critical_exit(primask);
}

/**
 * @brief 复位接收环形队列状态。
 * @param ctx 步进上下文。
 */
static void _rx_ring_reset(mod_stepper_ctx_t *ctx)
{
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验
    if (ctx == NULL)
    {
        return;
    }

    //2. 临界区内清空接收队列索引与计数
    primask = _critical_enter();
    ctx->rx_head = 0U;
    ctx->rx_tail = 0U;
    ctx->rx_count = 0U;
    _critical_exit(primask);
}

/**
 * @brief 将接收帧写入接收环形队列。
 * @param ctx 步进上下文。
 * @param buf 接收数据。
 * @param len 接收长度。
 */
static void _rx_ring_push(mod_stepper_ctx_t *ctx, const uint8_t *buf, uint16_t len)
{
    mod_stepper_rx_frame_t *slot; // 目标写入槽位
    uint16_t copy_len; // 实际拷贝长度
    uint32_t now; // 当前系统 tick
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验
    if ((ctx == NULL) || (buf == NULL) || (len == 0U))
    {
        return;
    }

    //2. 长度限幅到接收帧缓存上限
    copy_len = len;
    if (copy_len > MOD_STEPPER_RX_FRAME_BUF_SIZE)
    {
        copy_len = MOD_STEPPER_RX_FRAME_BUF_SIZE;
    }

    now = HAL_GetTick();

    //3. 临界区内写入环形队列，满队列时覆盖最旧帧
    primask = _critical_enter();

    if (ctx->rx_count >= MOD_STEPPER_RX_RING_SIZE)
    {
        ctx->rx_head++;
        if (ctx->rx_head >= MOD_STEPPER_RX_RING_SIZE)
        {
            ctx->rx_head = 0U;
        }
        ctx->rx_count--;
        ctx->rx_drop_count++;
    }

    slot = &ctx->rx_ring[ctx->rx_tail];
    slot->len = copy_len;
    memcpy(slot->data, buf, copy_len);
    slot->tick = now;

    ctx->rx_tail++;
    if (ctx->rx_tail >= MOD_STEPPER_RX_RING_SIZE)
    {
        ctx->rx_tail = 0U;
    }
    ctx->rx_count++;
    ctx->rx_push_count++;

    //4. 同步更新最近帧、ACK 与在线状态缓存
    memcpy(ctx->last_frame, buf, copy_len);
    ctx->last_frame_len = copy_len;
    ctx->has_new_frame = true;
    ctx->last_rx_tick = now;
    ctx->registered = true;

    if ((copy_len >= 4U) && (buf[copy_len - 1U] == MOD_STEPPER_FRAME_TAIL))
    {
        ctx->last_ack_cmd = buf[1U];
        ctx->last_ack_status = buf[2U];
        ctx->has_new_ack = true;
    }

    _critical_exit(primask);
}

/**
 * @brief 从接收环形队列弹出一帧。
 * @param ctx 步进上下文。
 * @param out_buf 输出缓冲区。
 * @param inout_len 输入缓冲区大小，输出实际长度。
 * @return true 读取成功。
 * @return false 队列空或参数无效。
 */
static bool _rx_ring_pop(mod_stepper_ctx_t *ctx, uint8_t *out_buf, uint16_t *inout_len)
{
    bool result = false; // 出队结果
    uint16_t copy_len; // 实际拷贝长度
    uint32_t primask; // 临界区状态保存值
    mod_stepper_rx_frame_t *slot; // 当前队首槽位

    //1. 参数校验
    if ((ctx == NULL) || (out_buf == NULL) || (inout_len == NULL) || (*inout_len == 0U))
    {
        return false;
    }

    //2. 临界区内读取队首并出队
    primask = _critical_enter();

    if (ctx->rx_count > 0U)
    {
        slot = &ctx->rx_ring[ctx->rx_head];
        copy_len = slot->len;
        if (copy_len > *inout_len)
        {
            copy_len = *inout_len;
        }

        memcpy(out_buf, slot->data, copy_len);
        *inout_len = copy_len;

        ctx->rx_head++;
        if (ctx->rx_head >= MOD_STEPPER_RX_RING_SIZE)
        {
            ctx->rx_head = 0U;
        }
        ctx->rx_count--;
        ctx->has_new_frame = (ctx->rx_count > 0U);
        result = true;
    }

    _critical_exit(primask);
    return result;
}

static bool _stepper_sys_param_to_cmd(mod_stepper_sys_param_e param, uint8_t *out_cmd)
{
    //1. 参数校验
    if (out_cmd == NULL)
    {
        return false;
    }

    //2. 系统参数枚举映射到协议命令码
    switch (param)
    {
    case MOD_STEPPER_SYS_VBUS:  *out_cmd = 0x24U; return true;
    case MOD_STEPPER_SYS_CBUS:  *out_cmd = 0x26U; return true;
    case MOD_STEPPER_SYS_CPHA:  *out_cmd = 0x27U; return true;
    case MOD_STEPPER_SYS_ENCO:  *out_cmd = 0x29U; return true;
    case MOD_STEPPER_SYS_CLKC:  *out_cmd = 0x30U; return true;
    case MOD_STEPPER_SYS_ENCL:  *out_cmd = 0x31U; return true;
    case MOD_STEPPER_SYS_CLKI:  *out_cmd = 0x32U; return true;
    case MOD_STEPPER_SYS_TPOS:  *out_cmd = 0x33U; return true;
    case MOD_STEPPER_SYS_SPOS:  *out_cmd = 0x34U; return true;
    case MOD_STEPPER_SYS_VEL:   *out_cmd = 0x35U; return true;
    case MOD_STEPPER_SYS_CPOS:  *out_cmd = 0x36U; return true;
    case MOD_STEPPER_SYS_PERR:  *out_cmd = 0x37U; return true;
    case MOD_STEPPER_SYS_VBAT:  *out_cmd = 0x38U; return true;
    case MOD_STEPPER_SYS_TEMP:  *out_cmd = 0x39U; return true;
    case MOD_STEPPER_SYS_FLAG:  *out_cmd = 0x3AU; return true;
    case MOD_STEPPER_SYS_OFLAG: *out_cmd = 0x3BU; return true;
    case MOD_STEPPER_SYS_OAF:   *out_cmd = 0x3CU; return true;
    case MOD_STEPPER_SYS_PIN:   *out_cmd = 0x3DU; return true;
    default:                    return false;
    }
}

static void _tx_refresh_state(mod_stepper_ctx_t *ctx)
{
    uint32_t now; // 当前系统 tick
    uint32_t delta; // 发送占用持续时间

    //1. 上下文与发送状态校验
    if (!_ctx_is_ready(ctx))
    {
        return;
    }

    if (!ctx->tx_active)
    {
        return;
    }

    //2. 若 UART 已空闲，结束当前发送激活态
    if (drv_uart_is_tx_free(ctx->bind.huart))
    {
        ctx->tx_active = false;
        ctx->tx_active_tick = 0U;
        return;
    }

    //3. 超时保护：发送长期占用则判定失败并复位状态
    now = HAL_GetTick();
    delta = now - ctx->tx_active_tick;
    if (delta > MOD_STEPPER_TX_ACTIVE_TIMEOUT_MS)
    {
        ctx->tx_active = false;
        ctx->tx_active_tick = 0U;
        ctx->tx_fail_count++;
    }
}

static void _kick_tx(mod_stepper_ctx_t *ctx)
{
    uint16_t len = 0U; // 待发送帧长度

    //1. 上下文校验并刷新发送状态
    if (!_ctx_is_ready(ctx))
    {
        return;
    }

    _tx_refresh_state(ctx);

    //2. 仅在“未激活且 UART 空闲”时尝试发送队首帧
    if (ctx->tx_active)
    {
        return;
    }

    if (!drv_uart_is_tx_free(ctx->bind.huart))
    {
        return;
    }

    //3. 取队首帧并尝试 DMA 发送
    if (!_tx_queue_peek(ctx, ctx->tx_active_buf, &len))
    {
        return;
    }

    //4. 发送成功后出队，失败累计失败计数
    if (drv_uart_send_dma(ctx->bind.huart, ctx->tx_active_buf, len))
    {
        ctx->tx_active = true;
        ctx->tx_active_tick = HAL_GetTick();
        _tx_queue_pop(ctx);
    }
    else
    {
        ctx->tx_fail_count++;
    }
}

static bool _send_frame_enqueue(mod_stepper_ctx_t *ctx, const uint8_t *frame, uint16_t len)
{
    //1. 参数和上下文校验
    if (!_ctx_is_ready(ctx) || (frame == NULL) || (len == 0U) || (len > MOD_STEPPER_TX_BUF_SIZE))
    {
        return false;
    }

    //2. 入队成功后尝试立即触发发送
    if (_tx_queue_push(ctx, frame, len))
    {
        _kick_tx(ctx);
        return true;
    }

    return false;
}

static void _ctx_reset_runtime(mod_stepper_ctx_t *ctx)
{
    //1. 参数校验
    if (ctx == NULL)
    {
        return;
    }

    //2. 清理 RX 侧缓存、环形队列与最近帧状态
    memset(ctx->rx_dma_buf, 0, sizeof(ctx->rx_dma_buf));
    memset(ctx->rx_ring, 0, sizeof(ctx->rx_ring));
    _rx_ring_reset(ctx);

    memset(ctx->last_frame, 0, sizeof(ctx->last_frame));
    ctx->last_frame_len = 0U;
    ctx->has_new_frame = false;
    ctx->last_ack_cmd = 0U;
    ctx->last_ack_status = 0U;
    ctx->has_new_ack = false;
    ctx->last_rx_tick = 0U;
    ctx->registered = false;

    //3. 清理 TX 侧队列、激活发送状态与统计计数
    memset(ctx->tx_queue, 0, sizeof(ctx->tx_queue));
    _tx_queue_reset(ctx);
    ctx->tx_active = false;
    memset(ctx->tx_active_buf, 0, sizeof(ctx->tx_active_buf));
    ctx->tx_active_tick = 0U;

    ctx->rx_push_count = 0U;
    ctx->rx_drop_count = 0U;
    ctx->tx_push_count = 0U;
    ctx->tx_drop_count = 0U;
    ctx->tx_fail_count = 0U;
}

static void _stepper_rx_callback_handler(mod_stepper_ctx_t *ctx, uint16_t len)
{
    uint16_t copy_len; // 实际搬运长度

    //1. 上下文校验
    if (!_ctx_is_ready(ctx))
    {
        return;
    }

    //2. 将 DMA 收到的数据写入接收环形队列，并通知上层信号量
    if (len > 0U)
    {
        copy_len = len;
        if (copy_len > MOD_STEPPER_RX_DMA_BUF_SIZE)
        {
            copy_len = MOD_STEPPER_RX_DMA_BUF_SIZE;
        }

        _rx_ring_push(ctx, ctx->rx_dma_buf, copy_len);

        if (ctx->bind.sem_id != NULL)
        {
            (void)osSemaphoreRelease(ctx->bind.sem_id);
        }
    }

    //3. 重新拉起 DMA 接收，失败时执行停启重试
    HAL_UART_DMAStop(ctx->bind.huart);
    memset(ctx->rx_dma_buf, 0, MOD_STEPPER_RX_DMA_BUF_SIZE);
    if (!drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_dma_buf, MOD_STEPPER_RX_DMA_BUF_SIZE))
    {
        drv_uart_receive_dma_stop(ctx->bind.huart);
        (void)drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_dma_buf, MOD_STEPPER_RX_DMA_BUF_SIZE);
    }

    //4. 处理接收后触发一次发送调度
    _kick_tx(ctx);
}

static void _stepper_rx_dispatch(uint8_t uart_idx, uint16_t len)
{
    //1. 按 UART 索引找到上下文并分发接收回调
    if (uart_idx < MOD_STEPPER_MAX_UART_INSTANCES)
    {
        mod_stepper_ctx_t *ctx = s_ctx_by_uart[uart_idx];
        if (ctx != NULL)
        {
            _stepper_rx_callback_handler(ctx, len);
        }
    }
}

static void _stepper_rx_cb_0(uint16_t len) { _stepper_rx_dispatch(0U, len); }
static void _stepper_rx_cb_1(uint16_t len) { _stepper_rx_dispatch(1U, len); }
static void _stepper_rx_cb_2(uint16_t len) { _stepper_rx_dispatch(2U, len); }
static void _stepper_rx_cb_3(uint16_t len) { _stepper_rx_dispatch(3U, len); }
static void _stepper_rx_cb_4(uint16_t len) { _stepper_rx_dispatch(4U, len); }
static void _stepper_rx_cb_5(uint16_t len) { _stepper_rx_dispatch(5U, len); }

static drv_uart_rx_callback_t s_stepper_rx_cbs[MOD_STEPPER_MAX_UART_INSTANCES] =
{
    _stepper_rx_cb_0,
    _stepper_rx_cb_1,
    _stepper_rx_cb_2,
    _stepper_rx_cb_3,
    _stepper_rx_cb_4,
    _stepper_rx_cb_5
};

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    //1. 空指针保护
    if (huart == NULL)
    {
        return;
    }

    //2. 匹配上下文并清除发送激活态，然后继续发送队列
    for (uint16_t i = 0U; i < MOD_STEPPER_MAX_UART_INSTANCES; i++)
    {
        mod_stepper_ctx_t *ctx = s_ctx_by_uart[i];
        if ((ctx != NULL) && _ctx_is_ready(ctx) && (ctx->bind.huart == huart))
        {
            ctx->tx_active = false;
            ctx->tx_active_tick = 0U;
            _kick_tx(ctx);
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    //1. 空指针保护
    if (huart == NULL)
    {
        return;
    }

    //2. 匹配上下文后复位发送状态并重启 DMA 接收
    for (uint16_t i = 0U; i < MOD_STEPPER_MAX_UART_INSTANCES; i++)
    {
        mod_stepper_ctx_t *ctx = s_ctx_by_uart[i];
        if ((ctx != NULL) && _ctx_is_ready(ctx) && (ctx->bind.huart == huart))
        {
            ctx->tx_active = false;
            ctx->tx_active_tick = 0U;
            HAL_UART_DMAStop(ctx->bind.huart);
            (void)drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_dma_buf, MOD_STEPPER_RX_DMA_BUF_SIZE);
            _kick_tx(ctx);
        }
    }
}

bool mod_stepper_init(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind)
{
    //1. 参数校验
    if (ctx == NULL)
    {
        return false;
    }

    //2. 清空上下文并标记已初始化
    memset(ctx, 0, sizeof(mod_stepper_ctx_t));
    ctx->inited = true;

    //3. 若提供绑定参数则直接执行绑定
    if (bind != NULL)
    {
        return mod_stepper_bind(ctx, bind);
    }

    return true;
}

bool mod_stepper_bind(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind)
{
    uint8_t logic_id;
    uint8_t driver_addr;
    int8_t new_idx;
    int8_t old_idx;
    bool new_claimed = false;
    bool result;

    //1. 入参校验
    if ((ctx == NULL) || !ctx->inited || (bind == NULL) || (bind->huart == NULL))
    {
        return false;
    }

    //2. 校验逻辑 ID 和驱动地址
    logic_id = bind->motor_id;
    if ((logic_id == 0U) || (logic_id > MOD_STEPPER_LOGIC_ID_MAX))
    {
        return false;
    }

    driver_addr = bind->driver_addr;
    if (driver_addr == 0U)
    {
        driver_addr = 1U;
    }

    //3. 校验 UART 映射与资源占用冲突
    new_idx = _get_uart_index(bind->huart->Instance);
    if (new_idx < 0)
    {
        return false;
    }

    if ((s_ctx_by_uart[(uint16_t)new_idx] != NULL) && (s_ctx_by_uart[(uint16_t)new_idx] != ctx))
    {
        return false;
    }

    if ((s_ctx_by_logic[logic_id] != NULL) && (s_ctx_by_logic[logic_id] != ctx))
    {
        return false;
    }

    //4. 申请 UART 归属权
    if (!mod_uart_guard_claim(bind->huart, MOD_UART_OWNER_STEPPER))
    {
        return false;
    }
    new_claimed = true;

    //5. 解绑旧资源映射（如存在）
    old_idx = _ctx_bound_uart_idx(ctx);
    if ((old_idx >= 0) && (ctx->bind.huart != NULL))
    {
        drv_uart_receive_dma_stop(ctx->bind.huart);
        (void)drv_uart_register_callback(ctx->bind.huart, NULL);
        if (ctx->bind.huart != bind->huart)
        {
            (void)mod_uart_guard_release(ctx->bind.huart, MOD_UART_OWNER_STEPPER);
        }
    }

    if ((old_idx >= 0) && (s_ctx_by_uart[(uint16_t)old_idx] == ctx))
    {
        s_ctx_by_uart[(uint16_t)old_idx] = NULL;
    }

    if ((ctx->bind.motor_id > 0U) && (ctx->bind.motor_id <= MOD_STEPPER_LOGIC_ID_MAX) && (s_ctx_by_logic[ctx->bind.motor_id] == ctx))
    {
        s_ctx_by_logic[ctx->bind.motor_id] = NULL;
    }

    //6. 写入新绑定配置并复位运行时状态
    ctx->bind = *bind;
    ctx->bind.motor_id = logic_id;
    ctx->bind.driver_addr = driver_addr;
    _ctx_reset_runtime(ctx);
    ctx->registered = true;

    s_ctx_by_uart[(uint16_t)new_idx] = ctx;
    s_ctx_by_logic[logic_id] = ctx;

    //7. 注册回调并启动 DMA 接收
    result = drv_uart_register_callback(ctx->bind.huart, s_stepper_rx_cbs[(uint16_t)new_idx]);
    if (result)
    {
        memset(ctx->rx_dma_buf, 0, MOD_STEPPER_RX_DMA_BUF_SIZE);
        result = drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_dma_buf, MOD_STEPPER_RX_DMA_BUF_SIZE);
    }

    //8. 启动失败时回滚资源占用与映射关系
    if (!result)
    {
        if (new_claimed)
        {
            (void)mod_uart_guard_release(bind->huart, MOD_UART_OWNER_STEPPER);
        }

        if (s_ctx_by_uart[(uint16_t)new_idx] == ctx)
        {
            s_ctx_by_uart[(uint16_t)new_idx] = NULL;
        }
        if (s_ctx_by_logic[logic_id] == ctx)
        {
            s_ctx_by_logic[logic_id] = NULL;
        }
        memset(&ctx->bind, 0, sizeof(ctx->bind));
        _ctx_reset_runtime(ctx);
    }
    return result;
}

void mod_stepper_unbind(mod_stepper_ctx_t *ctx)
{
    int8_t idx; // 当前绑定 UART 索引

    //1. 参数与初始化状态校验
    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    //2. 停止 DMA，注销回调并释放 UART 所有权
    idx = _ctx_bound_uart_idx(ctx);
    if ((idx >= 0) && (ctx->bind.huart != NULL))
    {
        drv_uart_receive_dma_stop(ctx->bind.huart);
        (void)drv_uart_register_callback(ctx->bind.huart, NULL);
        (void)mod_uart_guard_release(ctx->bind.huart, MOD_UART_OWNER_STEPPER);
    }

    //3. 清除映射关系并复位上下文绑定状态
    if ((idx >= 0) && (s_ctx_by_uart[(uint16_t)idx] == ctx))
    {
        s_ctx_by_uart[(uint16_t)idx] = NULL;
    }

    if ((ctx->bind.motor_id > 0U) && (ctx->bind.motor_id <= MOD_STEPPER_LOGIC_ID_MAX) && (s_ctx_by_logic[ctx->bind.motor_id] == ctx))
    {
        s_ctx_by_logic[ctx->bind.motor_id] = NULL;
    }

    memset(&ctx->bind, 0, sizeof(ctx->bind));
    _ctx_reset_runtime(ctx);
}

const mod_stepper_bind_t *mod_stepper_get_bind(const mod_stepper_ctx_t *ctx)
{
    //1. 参数与初始化状态校验
    if ((ctx == NULL) || !ctx->inited)
    {
        return NULL;
    }

    //2. 返回当前绑定配置
    return &ctx->bind;
}

bool mod_stepper_start_dma_rx(mod_stepper_ctx_t *ctx)
{
    //1. 仅对已绑定上下文启动 DMA 接收
    if (!_ctx_is_ready(ctx))
    {
        return false;
    }

    //2. 清空 DMA 缓冲并启动接收
    memset(ctx->rx_dma_buf, 0, MOD_STEPPER_RX_DMA_BUF_SIZE);
    return drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_dma_buf, MOD_STEPPER_RX_DMA_BUF_SIZE);
}

void mod_stepper_stop_dma_rx(mod_stepper_ctx_t *ctx)
{
    //1. 仅对已绑定上下文停止 DMA 接收
    if (_ctx_is_ready(ctx))
    {
        drv_uart_receive_dma_stop(ctx->bind.huart);
    }
}

bool mod_stepper_is_bound(const mod_stepper_ctx_t *ctx)
{
    //1. 返回上下文是否处于可工作绑定状态
    return _ctx_is_ready(ctx);
}

void mod_stepper_process(mod_stepper_ctx_t *ctx)
{
    //1. 指定上下文时仅处理该上下文发送队列
    if (ctx != NULL)
    {
        _kick_tx(ctx);
        return;
    }

    //2. ctx 为 NULL 时轮询处理全部上下文
    for (uint16_t i = 0U; i < MOD_STEPPER_MAX_UART_INSTANCES; i++)
    {
        _kick_tx(s_ctx_by_uart[i]);
    }
}

bool mod_stepper_register_motor(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    //1. 解析目标上下文并标记已注册
    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    ctx->registered = true;
    return true;
}

bool mod_stepper_unregister_motor(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    //1. 解析目标上下文并清除注册标志
    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    ctx->registered = false;
    return true;
}

uint8_t mod_stepper_get_registered_motor_count(const mod_stepper_ctx_t *ctx)
{
    //1. 未就绪上下文返回 0
    if (!_ctx_is_ready(ctx))
    {
        return 0U;
    }

    //2. 单上下文实现：注册则返回 1，否则返回 0
    return ctx->registered ? 1U : 0U;
}

bool mod_stepper_is_motor_online(const mod_stepper_ctx_t *ctx, uint8_t addr, uint32_t timeout_ms)
{
    const mod_stepper_ctx_t *target; // 解析得到的目标上下文
    uint32_t now; // 当前系统 tick

    //1. 解析上下文并校验是否已注册
    target = _resolve_ctx_const(ctx, addr);
    if ((target == NULL) || !target->registered)
    {
        return false;
    }

    //2. 以最近接收时间戳判断在线状态
    now = HAL_GetTick();
    return ((now - target->last_rx_tick) <= timeout_ms);
}

bool mod_stepper_set_driver_addr(mod_stepper_ctx_t *ctx, uint8_t addr, uint8_t driver_addr)
{
    //1. 解析上下文并校验新地址
    ctx = _resolve_ctx(ctx, addr);
    if ((ctx == NULL) || (driver_addr == 0U))
    {
        return false;
    }

    //2. 更新驱动地址缓存
    ctx->bind.driver_addr = driver_addr;
    return true;
}

uint8_t mod_stepper_get_driver_addr(const mod_stepper_ctx_t *ctx)
{
    //1. 上下文未就绪返回 0
    if (!_ctx_is_ready(ctx))
    {
        return 0U;
    }

    //2. 返回当前缓存驱动地址
    return ctx->bind.driver_addr;
}

bool mod_stepper_send_raw(mod_stepper_ctx_t *ctx, const uint8_t *buf, uint16_t len)
{
    //1. 原始帧发送：直接入队并调度发送
    return _send_frame_enqueue(ctx, buf, len);
}

bool mod_stepper_trigger_encoder_cal(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    uint8_t frame[4];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x06U;
    frame[2] = 0x45U;
    frame[3] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 4U);
}

bool mod_stepper_reset_motor(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    uint8_t frame[4];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x08U;
    frame[2] = 0x97U;
    frame[3] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 4U);
}

bool mod_stepper_reset_clog_pro(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    uint8_t frame[4];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x0EU;
    frame[2] = 0x52U;
    frame[3] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 4U);
}

bool mod_stepper_restore_motor(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    uint8_t frame[4];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x0FU;
    frame[2] = 0x5FU;
    frame[3] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 4U);
}

bool mod_stepper_modify_motor_id_single(mod_stepper_ctx_t *ctx, uint8_t old_addr, bool save_flag, uint8_t new_addr)
{
    bool result; // 命令发送结果
    uint8_t frame[6]; // 修改地址命令帧

    //1. 参数与上下文解析
    if (new_addr == 0U)
    {
        return false;
    }

    ctx = _resolve_ctx(ctx, old_addr);
    if (ctx == NULL)
    {
        return false;
    }

    //2. 组包并发送
    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0xAEU;
    frame[2] = 0x4BU;
    frame[3] = (uint8_t)save_flag;
    frame[4] = new_addr;
    frame[5] = MOD_STEPPER_FRAME_TAIL;

    //3. 发送成功后同步更新缓存地址
    result = _send_frame_enqueue(ctx, frame, 6U);
    if (result)
    {
        ctx->bind.driver_addr = new_addr;
    }

    return result;
}

bool mod_stepper_enable(mod_stepper_ctx_t *ctx, uint8_t addr, bool enable, bool sync_flag)
{
    uint8_t frame[6];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0xF3U;
    frame[2] = 0xABU;
    frame[3] = (uint8_t)enable;
    frame[4] = (uint8_t)sync_flag;
    frame[5] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 6U);
}

bool mod_stepper_velocity(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag)
{
    uint8_t frame[8]; // 速度模式命令帧

    //1. 解析上下文
    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    //2. 速度限幅后组包发送
    if (vel_rpm > MOD_STEPPER_VEL_MAX_RPM)
    {
        vel_rpm = MOD_STEPPER_VEL_MAX_RPM;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0xF6U;
    frame[2] = (uint8_t)dir;
    frame[3] = (uint8_t)(vel_rpm >> 8);
    frame[4] = (uint8_t)(vel_rpm >> 0);
    frame[5] = acc;
    frame[6] = (uint8_t)sync_flag;
    frame[7] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 8U);
}

bool mod_stepper_position(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag)
{
    uint8_t frame[13]; // 位置模式命令帧

    //1. 解析上下文
    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    //2. 速度限幅后组包发送
    if (vel_rpm > MOD_STEPPER_VEL_MAX_RPM)
    {
        vel_rpm = MOD_STEPPER_VEL_MAX_RPM;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0xFDU;
    frame[2] = (uint8_t)dir;
    frame[3] = (uint8_t)(vel_rpm >> 8);
    frame[4] = (uint8_t)(vel_rpm >> 0);
    frame[5] = acc;
    frame[6] = (uint8_t)(pulse >> 24);
    frame[7] = (uint8_t)(pulse >> 16);
    frame[8] = (uint8_t)(pulse >> 8);
    frame[9] = (uint8_t)(pulse >> 0);
    frame[10] = (uint8_t)absolute_mode;
    frame[11] = (uint8_t)sync_flag;
    frame[12] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 13U);
}

bool mod_stepper_stop(mod_stepper_ctx_t *ctx, uint8_t addr, bool sync_flag)
{
    uint8_t frame[5];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0xFEU;
    frame[2] = 0x98U;
    frame[3] = (uint8_t)sync_flag;
    frame[4] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 5U);
}

bool mod_stepper_sync_start(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    uint8_t frame[4];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0xFFU;
    frame[2] = 0x66U;
    frame[3] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 4U);
}

bool mod_stepper_reset_cur_pos_to_zero(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    uint8_t frame[4];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x0AU;
    frame[2] = 0x6DU;
    frame[3] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 4U);
}

bool mod_stepper_origin_set_zero(mod_stepper_ctx_t *ctx, uint8_t addr, bool save_flag)
{
    uint8_t frame[5];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x93U;
    frame[2] = 0x88U;
    frame[3] = (uint8_t)save_flag;
    frame[4] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 5U);
}

bool mod_stepper_origin_trigger(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_origin_mode_e mode, bool sync_flag)
{
    uint8_t frame[5]; // 回零触发命令帧

    //1. 解析上下文并校验回零模式
    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    if ((uint8_t)mode > (uint8_t)MOD_STEPPER_ORIGIN_MODE_LIMIT_BACK)
    {
        return false;
    }

    //2. 组包并发送
    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x9AU;
    frame[2] = (uint8_t)mode;
    frame[3] = (uint8_t)sync_flag;
    frame[4] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 5U);
}

bool mod_stepper_origin_interrupt(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    uint8_t frame[4];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x9CU;
    frame[2] = 0x48U;
    frame[3] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 4U);
}

bool mod_stepper_origin_read_params(mod_stepper_ctx_t *ctx, uint8_t addr)
{
    uint8_t frame[3];

    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x22U;
    frame[2] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 3U);
}

bool mod_stepper_origin_modify_params(mod_stepper_ctx_t *ctx, uint8_t addr, bool save_flag, mod_stepper_origin_mode_e mode, mod_stepper_dir_e dir, uint16_t origin_vel_rpm, uint32_t origin_timeout_ms, uint16_t collide_vel_rpm, uint16_t collide_current_ma, uint16_t collide_time_ms, bool power_on_trigger)
{
    uint8_t frame[20]; // 回零参数配置命令帧

    //1. 解析上下文并校验回零模式
    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    if ((uint8_t)mode > (uint8_t)MOD_STEPPER_ORIGIN_MODE_LIMIT_BACK)
    {
        return false;
    }

    //2. 关键速度参数限幅
    if (origin_vel_rpm > MOD_STEPPER_VEL_MAX_RPM)
    {
        origin_vel_rpm = MOD_STEPPER_VEL_MAX_RPM;
    }

    if (collide_vel_rpm > MOD_STEPPER_VEL_MAX_RPM)
    {
        collide_vel_rpm = MOD_STEPPER_VEL_MAX_RPM;
    }

    //3. 组包并发送
    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x4CU;
    frame[2] = 0xAEU;
    frame[3] = (uint8_t)save_flag;
    frame[4] = (uint8_t)mode;
    frame[5] = (uint8_t)dir;
    frame[6] = (uint8_t)(origin_vel_rpm >> 8);
    frame[7] = (uint8_t)(origin_vel_rpm >> 0);
    frame[8] = (uint8_t)(origin_timeout_ms >> 24);
    frame[9] = (uint8_t)(origin_timeout_ms >> 16);
    frame[10] = (uint8_t)(origin_timeout_ms >> 8);
    frame[11] = (uint8_t)(origin_timeout_ms >> 0);
    frame[12] = (uint8_t)(collide_vel_rpm >> 8);
    frame[13] = (uint8_t)(collide_vel_rpm >> 0);
    frame[14] = (uint8_t)(collide_current_ma >> 8);
    frame[15] = (uint8_t)(collide_current_ma >> 0);
    frame[16] = (uint8_t)(collide_time_ms >> 8);
    frame[17] = (uint8_t)(collide_time_ms >> 0);
    frame[18] = (uint8_t)power_on_trigger;
    frame[19] = MOD_STEPPER_FRAME_TAIL;

    return _send_frame_enqueue(ctx, frame, 20U);
}

bool mod_stepper_read_sys_param(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_sys_param_e param)
{
    uint8_t cmd = 0U; // 协议命令码
    uint8_t frame[3]; // 读系统参数命令帧

    //1. 解析上下文与参数命令映射
    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    if (!_stepper_sys_param_to_cmd(param, &cmd))
    {
        return false;
    }

    //2. 组包并发送
    frame[0] = ctx->bind.driver_addr;
    frame[1] = cmd;
    frame[2] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 3U);
}

bool mod_stepper_auto_return_sys_param_timed(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_sys_param_e param, uint16_t period_ms)
{
    uint8_t cmd = 0U; // 协议命令码
    uint8_t frame[7]; // 定时主动上报参数命令帧

    //1. 解析上下文与参数命令映射
    ctx = _resolve_ctx(ctx, addr);
    if (ctx == NULL)
    {
        return false;
    }

    if (!_stepper_sys_param_to_cmd(param, &cmd))
    {
        return false;
    }

    //2. 组包并发送
    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0x11U;
    frame[2] = 0x18U;
    frame[3] = cmd;
    frame[4] = (uint8_t)(period_ms >> 8);
    frame[5] = (uint8_t)(period_ms >> 0);
    frame[6] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame_enqueue(ctx, frame, 7U);
}

bool mod_stepper_has_new_frame(const mod_stepper_ctx_t *ctx)
{
    //1. 上下文未就绪则无新帧
    if (!_ctx_is_ready(ctx))
    {
        return false;
    }

    //2. 根据接收队列计数判断
    return (ctx->rx_count > 0U);
}

bool mod_stepper_get_last_frame(mod_stepper_ctx_t *ctx, uint8_t *out_buf, uint16_t *inout_len)
{
    if (!_ctx_is_ready(ctx))
    {
        return false;
    }

    return _rx_ring_pop(ctx, out_buf, inout_len);
}

bool mod_stepper_get_last_ack(mod_stepper_ctx_t *ctx, uint8_t *cmd, uint8_t *status)
{
    bool result = false; // 是否取到新的 ACK
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验
    if ((ctx == NULL) || (cmd == NULL) || (status == NULL))
    {
        return false;
    }

    //2. 临界区读取 ACK 并清除“新 ACK”标志
    primask = _critical_enter();
    if (ctx->has_new_ack)
    {
        *cmd = ctx->last_ack_cmd;
        *status = ctx->last_ack_status;
        ctx->has_new_ack = false;
        result = true;
    }
    _critical_exit(primask);

    return result;
}

bool mod_stepper_has_new_frame_by_addr(const mod_stepper_ctx_t *ctx, uint8_t addr)
{
    const mod_stepper_ctx_t *target = _resolve_ctx_const(ctx, addr); // 目标上下文

    //1. 解析失败返回 false
    if (target == NULL)
    {
        return false;
    }

    //2. 根据目标上下文接收队列计数判断
    return (target->rx_count > 0U);
}

bool mod_stepper_get_last_frame_by_addr(mod_stepper_ctx_t *ctx, uint8_t addr, uint8_t *out_buf, uint16_t *inout_len)
{
    mod_stepper_ctx_t *target = _resolve_ctx(ctx, addr);

    if (target == NULL)
    {
        return false;
    }

    return _rx_ring_pop(target, out_buf, inout_len);
}

bool mod_stepper_get_last_ack_by_addr(mod_stepper_ctx_t *ctx, uint8_t addr, uint8_t *cmd, uint8_t *status)
{
    bool result = false; // 是否取到新的 ACK
    uint32_t primask; // 临界区状态保存值
    mod_stepper_ctx_t *target = _resolve_ctx(ctx, addr); // 目标上下文

    //1. 参数与上下文校验
    if ((target == NULL) || (cmd == NULL) || (status == NULL))
    {
        return false;
    }

    //2. 临界区读取目标 ACK 并清除“新 ACK”标志
    primask = _critical_enter();
    if (target->has_new_ack)
    {
        *cmd = target->last_ack_cmd;
        *status = target->last_ack_status;
        target->has_new_ack = false;
        result = true;
    }
    _critical_exit(primask);

    return result;
}
