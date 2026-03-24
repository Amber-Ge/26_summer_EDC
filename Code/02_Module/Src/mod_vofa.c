/**
 * @file    mod_vofa.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   VOFA 通信模块实现。
 * @details
 * 1. 文件作用：实现 VOFA 通道上下文管理、命令解析和多类型数据打包发送。
 * 2. 解耦边界：协议处理在模块内部闭环，不直接执行上层业务命令动作。
 * 3. 上层绑定：`InitTask` 注入串口与互斥资源，业务任务按需调用发送/读命令接口。
 * 4. 下层依赖：`drv_uart` 提供 DMA/阻塞收发，`mod_uart_guard` 负责 UART 独占仲裁。
 * 5. 生命周期：先 `ctx_init/bind`，运行期由回调更新命令状态，最后按需解绑。
 */

#include "mod_vofa.h"


typedef struct
{
    const char *cmd_str; // 命令文本
    vofa_cmd_id_t cmd_id; // 对应的命令ID
} vofa_cmd_entry_t;

static mod_vofa_ctx_t s_default_ctx; // 默认上下文实例

static const vofa_cmd_entry_t s_cmd_table[] =
{
    {"start", VOFA_CMD_START},
    {"stop",  VOFA_CMD_STOP}
};

#define CMD_TABLE_SIZE (sizeof(s_cmd_table) / sizeof(s_cmd_table[0]))

/**
 * @brief 进入临界区并返回进入前 PRIMASK。
 */
static uint32_t _critical_enter(void)
{
    uint32_t primask = __get_PRIMASK(); // 进入前中断屏蔽状态
    __disable_irq();
    return primask;
}

/**
 * @brief 退出临界区并恢复 PRIMASK。
 * @param primask 进入临界区前保存的 PRIMASK。
 */
static void _critical_exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/**
 * @brief 判断上下文是否处于可工作状态。
 * @details 要求 inited/bound/huart 全部有效。
 */
static bool _ctx_ready(const mod_vofa_ctx_t *ctx)
{
    // 判定上下文是否已初始化、已绑定且 UART 有效
    return ((ctx != NULL) &&
            ctx->inited &&
            ctx->bound &&
            (ctx->bind.huart != NULL));
}

/**
 * @brief 复制并清洗绑定配置到上下文。
 * @details 仅复制有效信号量，数量上限为 `MOD_VOFA_MAX_BIND_SEM`。
 */
static void _copy_bind_cfg(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind)
{
    // 复制 UART、互斥锁与信号量绑定配置
    ctx->bind.huart = bind->huart;
    ctx->bind.tx_mutex = bind->tx_mutex;
    ctx->bind.sem_count = 0U;
    memset(ctx->bind.sem_list, 0, sizeof(ctx->bind.sem_list));

    // 仅复制有效信号量句柄，并限制最大数量
    for (uint8_t i = 0U; (i < bind->sem_count) && (i < MOD_VOFA_MAX_BIND_SEM); i++) // 循环计数器
    {
        if (bind->sem_list[i] != NULL)
        {
            ctx->bind.sem_list[ctx->bind.sem_count] = bind->sem_list[i];
            ctx->bind.sem_count++;
        }
    }
}

/**
 * @brief 向所有已绑定信号量广播“有新命令”事件。
 */
static void _notify_all_semaphores(const mod_vofa_ctx_t *ctx)
{
    // 逐个释放绑定信号量，通知上层有新命令
    for (uint8_t i = 0U; i < ctx->bind.sem_count; i++) // 循环计数器
    {
        if (ctx->bind.sem_list[i] != NULL)
        {
            (void)osSemaphoreRelease(ctx->bind.sem_list[i]);
        }
    }
}

/**
 * @brief 重启 VOFA RX DMA 接收链路。
 * @details 优先使用 `restart_ex`，失败时再退化尝试 `start_ex`。
 */
static void _restart_rx_dma(mod_vofa_ctx_t *ctx)
{
    // 上下文就绪时执行 stop+start 重启，避免模块层直接调用 HAL 接口
    if (!_ctx_ready(ctx))
    {
        return;
    }

    if (drv_uart_receive_dma_restart_ex(ctx->bind.huart, ctx->rx_buf, MOD_VOFA_RX_BUF_SIZE) != DRV_UART_OK)
    {
        (void)drv_uart_receive_dma_start_ex(ctx->bind.huart, ctx->rx_buf, MOD_VOFA_RX_BUF_SIZE);
    }
}

/**
 * @brief 向发送缓存追加单字符。
 * @param max 缓冲可写上限（不含结尾 `\\0`）。
 * @param p_idx 当前写指针（成功后自增）。
 * @param c 待追加字符。
 */
static bool _append_char(mod_vofa_ctx_t *ctx, uint16_t max, uint16_t *p_idx, char c)
{
    // 参数与缓冲区边界校验
    if ((ctx == NULL) || (p_idx == NULL) || (*p_idx >= max))
    {
        return false;
    }

    // 追加单字符并更新写入索引
    ctx->tx_buf[*p_idx] = c;
    (*p_idx)++;
    return true;
}

/**
 * @brief 向发送缓存追加字符串。
 * @param max 缓冲可写上限（不含结尾 `\\0`）。
 * @param p_idx 当前写指针（成功后推进到末尾）。
 * @param s 待追加字符串，允许传 NULL（视为追加空串）。
 */
static bool _append_str(mod_vofa_ctx_t *ctx, uint16_t max, uint16_t *p_idx, const char *s)
{
    bool result = true; // 追加结果

    // 空字符串视为追加成功
    if (s == NULL)
    {
        return true;
    }

    // 逐字符追加，任一步失败则终止
    while ((*s != '\0') && result)
    {
        result = _append_char(ctx, max, p_idx, *s);
        s++;
    }

    return result;
}

/**
 * @brief 获取发送互斥锁。
 * @details 若未配置互斥锁，视为加锁成功。
 */
static bool _tx_lock(mod_vofa_ctx_t *ctx)
{
    // 未配置互斥锁时默认加锁成功
    if (ctx->bind.tx_mutex == NULL)
    {
        return true;
    }

    return (osMutexAcquire(ctx->bind.tx_mutex, MOD_VOFA_TX_MUTEX_TIMEOUT_MS) == osOK);
}

/**
 * @brief 释放发送互斥锁。
 */
static void _tx_unlock(mod_vofa_ctx_t *ctx)
{
    // 已配置互斥锁时释放发送锁
    if (ctx->bind.tx_mutex != NULL)
    {
        (void)osMutexRelease(ctx->bind.tx_mutex);
    }
}

/**
 * @brief VOFA RX 事件回调。
 * @details
 * 1. 仅处理归属本 ctx 的 UART 事件。
 * 2. 忽略 HT 事件，避免解析半帧数据。
 * 3. 解析到 start/stop 后更新 `last_cmd` 并广播信号量。
 */
static void _vofa_rx_callback_handler(UART_HandleTypeDef *huart,
                                    drv_uart_rx_event_t event,
                                    const uint8_t *data,
                                    uint16_t len,
                                    void *user_ctx)
{
    mod_vofa_ctx_t *ctx = (mod_vofa_ctx_t *)user_ctx; // 当前回调归属上下文

    // 上下文校验
    if (!_ctx_ready(ctx))
    {
        return;
    }

    // 回调归属校验，避免误处理其他 UART 的事件
    if (ctx->bind.huart != huart)
    {
        return;
    }

    // 忽略半传输事件，避免读取不完整帧
    if (event == DRV_UART_RX_EVENT_HT)
    {
        return;
    }

    // 接收长度限幅保护
    if (len > MOD_VOFA_RX_BUF_SIZE)
    {
        len = MOD_VOFA_RX_BUF_SIZE;
    }

    // 若驱动回调给出的 data 指针不是 ctx->rx_buf，则先复制到本地缓冲
    if ((data != NULL) && (data != ctx->rx_buf) && (len > 0U))
    {
        memcpy(ctx->rx_buf, data, len);
    }

    // 解析命令文本，匹配成功则更新命令并通知上层
    if (len > 0U)
    {
        if (len < MOD_VOFA_RX_BUF_SIZE)
        {
            ctx->rx_buf[len] = '\0';
        }
        else
        {
            ctx->rx_buf[MOD_VOFA_RX_BUF_SIZE - 1U] = '\0';
        }

        for (uint8_t i = 0U; i < (uint8_t)CMD_TABLE_SIZE; i++) // 循环计数器
        {
            if (strstr((const char *)ctx->rx_buf, s_cmd_table[i].cmd_str) != NULL)
            {
                ctx->last_cmd = s_cmd_table[i].cmd_id;
                _notify_all_semaphores(ctx);

                // 1 匹配到命令后重置 DMA 缓冲并立即重启接收
                memset(ctx->rx_buf, 0, MOD_VOFA_RX_BUF_SIZE);
                _restart_rx_dma(ctx);
                return;
            }
        }
    }

    // 未匹配命令时继续重启 DMA 接收
    _restart_rx_dma(ctx);
}

mod_vofa_ctx_t *mod_vofa_get_default_ctx(void)
{
    return &s_default_ctx;
}

/**
 * @brief 初始化上下文，可选立即绑定。
 * @param ctx VOFA 模块上下文指针。
 * @param bind 绑定参数（串口/互斥锁/信号量配置）。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_ctx_init(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind)
{
    // 参数校验
    if (ctx == NULL)
    {
        return false;
    }

    // 清空上下文并标记初始化完成
    memset(ctx, 0, sizeof(mod_vofa_ctx_t));
    ctx->inited = true;
    ctx->last_cmd = VOFA_CMD_NONE;

    // 若提供绑定参数则直接执行绑定
    if (bind != NULL)
    {
        return mod_vofa_bind(ctx, bind);
    }

    return true;
}

/**
 * @brief 反初始化 VOFA 上下文并释放绑定资源。
 * @param ctx VOFA 模块上下文指针。
 */
void mod_vofa_ctx_deinit(mod_vofa_ctx_t *ctx)
{
    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    mod_vofa_unbind(ctx);
    memset(ctx, 0, sizeof(mod_vofa_ctx_t));
}

/**
 * @brief 绑定上下文到 UART 并启动接收链路。
 * @param ctx VOFA 模块上下文指针。
 * @param bind 绑定参数（串口/互斥锁/信号量配置）。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_bind(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind)
{
    bool result = false; // 绑定结果
    drv_uart_status_t drv_status; // 驱动返回状态码

    // 参数与初始化状态校验
    if ((ctx == NULL) || !ctx->inited || (bind == NULL) || (bind->huart == NULL))
    {
        return false;
    }

    // 已绑定则先解绑旧资源
    if (ctx->bound)
    {
        mod_vofa_unbind(ctx);
    }

    // 申请 UART 资源所有权
    if (!mod_uart_guard_claim_ctx(bind->huart, MOD_UART_OWNER_VOFA, ctx))
    {
        return false;
    }

    // 复制绑定配置并清理收发缓存
    _copy_bind_cfg(ctx, bind);
    ctx->last_cmd = VOFA_CMD_NONE;
    memset(ctx->rx_buf, 0, sizeof(ctx->rx_buf));
    memset(ctx->tx_buf, 0, sizeof(ctx->tx_buf));

    // 注册回调并启动 DMA 接收
    drv_status = drv_uart_register_rx_callback(ctx->bind.huart, _vofa_rx_callback_handler, ctx);
    if (drv_status == DRV_UART_OK)
    {
        drv_status = drv_uart_receive_dma_start_ex(ctx->bind.huart, ctx->rx_buf, MOD_VOFA_RX_BUF_SIZE);
    }
    result = (drv_status == DRV_UART_OK);

    // 失败回滚资源，成功更新 bound 状态
    if (!result)
    {
        (void)drv_uart_unregister_rx_callback(ctx->bind.huart);
        (void)mod_uart_guard_release_ctx(ctx->bind.huart, MOD_UART_OWNER_VOFA, ctx);
        memset(&ctx->bind, 0, sizeof(ctx->bind));
        ctx->bound = false;
    }
    else
    {
        ctx->bound = true;
    }

    return result;
}

/**
 * @brief 解绑上下文并释放 UART 资源。
 * @param ctx VOFA 模块上下文指针。
 * @return 无。
 */
void mod_vofa_unbind(mod_vofa_ctx_t *ctx)
{
    // 参数与状态校验
    if ((ctx == NULL) || !ctx->inited || !ctx->bound || (ctx->bind.huart == NULL))
    {
        return;
    }

    // 停止 DMA、注销回调并释放 UART 资源
    (void)drv_uart_receive_dma_stop_ex(ctx->bind.huart);
    (void)drv_uart_unregister_rx_callback(ctx->bind.huart);
    (void)mod_uart_guard_release_ctx(ctx->bind.huart, MOD_UART_OWNER_VOFA, ctx);

    memset(&ctx->bind, 0, sizeof(ctx->bind));
    memset(ctx->rx_buf, 0, sizeof(ctx->rx_buf));
    ctx->bound = false;
    ctx->last_cmd = VOFA_CMD_NONE;
}

/**
 * @brief 查询上下文是否已就绪。
 * @param ctx VOFA 模块上下文指针。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_is_bound(const mod_vofa_ctx_t *ctx)
{
    // 返回上下文是否处于可工作绑定状态
    return _ctx_ready(ctx);
}

/**
 * @brief 添加通知信号量。
 * @param ctx VOFA 模块上下文指针。
 * @param sem_id 待添加或移除的信号量句柄。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_add_semaphore(mod_vofa_ctx_t *ctx, osSemaphoreId_t sem_id)
{
    bool result = false; // 添加结果
    uint32_t primask; // 临界区状态保存值

    // 参数与上下文校验
    if (!_ctx_ready(ctx) || (sem_id == NULL))
    {
        return false;
    }

    // 临界区内：已存在则成功，不存在则尝试追加
    primask = _critical_enter();

    for (uint8_t i = 0U; i < ctx->bind.sem_count; i++) // 循环计数器
    {
        if (ctx->bind.sem_list[i] == sem_id)
        {
            result = true;
            break;
        }
    }

    if (!result && (ctx->bind.sem_count < MOD_VOFA_MAX_BIND_SEM))
    {
        ctx->bind.sem_list[ctx->bind.sem_count] = sem_id;
        ctx->bind.sem_count++;
        result = true;
    }

    _critical_exit(primask);
    return result;
}

/**
 * @brief 删除通知信号量。
 * @param ctx VOFA 模块上下文指针。
 * @param sem_id 待添加或移除的信号量句柄。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_remove_semaphore(mod_vofa_ctx_t *ctx, osSemaphoreId_t sem_id)
{
    bool result = false; // 删除结果
    uint32_t primask; // 临界区状态保存值

    // 参数与上下文校验
    if (!_ctx_ready(ctx) || (sem_id == NULL))
    {
        return false;
    }

    // 临界区内查找并删除，后续元素前移
    primask = _critical_enter();

    for (uint8_t i = 0U; i < ctx->bind.sem_count; i++) // 循环计数器
    {
        if (ctx->bind.sem_list[i] == sem_id)
        {
            for (uint8_t j = i; (j + 1U) < ctx->bind.sem_count; j++) // 循环计数器
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

/**
 * @brief 清空通知信号量列表。
 * @param ctx VOFA 模块上下文指针。
 * @return 无。
 */
void mod_vofa_clear_semaphores(mod_vofa_ctx_t *ctx)
{
    uint32_t primask; // 临界区状态保存值

    // 上下文校验
    if (!_ctx_ready(ctx))
    {
        return;
    }

    // 清空信号量列表和计数
    primask = _critical_enter();
    memset(ctx->bind.sem_list, 0, sizeof(ctx->bind.sem_list));
    ctx->bind.sem_count = 0U;
    _critical_exit(primask);
}

/**
 * @brief 更新发送互斥锁配置。
 * @param ctx VOFA 模块上下文指针。
 * @param mutex_id 发送互斥锁句柄（可为 NULL）。
 * @return 无。
 */
void mod_vofa_set_tx_mutex(mod_vofa_ctx_t *ctx, osMutexId_t mutex_id)
{
    uint32_t primask; // 临界区状态保存值

    // 参数与初始化状态校验
    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    // 原子更新互斥锁句柄
    primask = _critical_enter();
    ctx->bind.tx_mutex = mutex_id;
    _critical_exit(primask);
}

/**
 * @brief 读取并清空最近命令。
 * @param ctx VOFA 模块上下文指针。
 * @return vofa_cmd_id_t 解析得到的命令标识。
 */
vofa_cmd_id_t mod_vofa_get_command_ctx(mod_vofa_ctx_t *ctx)
{
    vofa_cmd_id_t cmd = VOFA_CMD_NONE; // 返回命令
    uint32_t primask; // 临界区状态保存值

    // 参数与初始化状态校验
    if ((ctx == NULL) || !ctx->inited)
    {
        return VOFA_CMD_NONE;
    }

    // 读取并清空“最后命令”字段
    primask = _critical_enter();
    cmd = ctx->last_cmd;
    ctx->last_cmd = VOFA_CMD_NONE;
    _critical_exit(primask);

    return cmd;
}

/**
 * @brief 发送字符串数据帧。
 * @param ctx VOFA 模块上下文指针。
 * @param str 待发送字符串。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_string_ctx(mod_vofa_ctx_t *ctx, const char *str)
{
    bool result = false; // 发送结果
    uint16_t idx = 0U; // 发送缓冲写入索引
    const uint16_t max_payload = (uint16_t)(MOD_VOFA_TX_BUF_SIZE - 1U); // 最大有效负载长度

    // 参数与上下文校验
    if (!_ctx_ready(ctx) || (str == NULL))
    {
        return false;
    }

    // 获取发送锁
    if (!_tx_lock(ctx))
    {
        return false;
    }

    // 串口空闲时组包并 DMA 发送
    if (drv_uart_is_tx_free(ctx->bind.huart))
    {
        result = _append_str(ctx, max_payload, &idx, str);
        if (result)
        {
            result = _append_char(ctx, max_payload, &idx, '\n');
        }
        if (result)
        {
            result = (drv_uart_send_dma_ex(ctx->bind.huart, (const uint8_t *)ctx->tx_buf, idx) == DRV_UART_OK);
        }
    }

    _tx_unlock(ctx);
    return result;
}

/**
 * @brief 发送浮点数组数据帧。
 * @param ctx VOFA 模块上下文指针。
 * @param tag 数据标签（可为 NULL）。
 * @param arr 待发送数组首地址。
 * @param n 数组元素个数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_float_ctx(mod_vofa_ctx_t *ctx, const char *tag, const float *arr, uint16_t n)
{
    bool result = false; // 发送结果
    uint16_t idx = 0U; // 发送缓冲写入索引
    const uint16_t max_payload = (uint16_t)(MOD_VOFA_TX_BUF_SIZE - 1U); // 最大有效负载长度

    // 参数与上下文校验
    if (!_ctx_ready(ctx) || (arr == NULL) || (n == 0U))
    {
        return false;
    }

    // 获取发送锁
    if (!_tx_lock(ctx))
    {
        return false;
    }

    // 串口空闲时组装 "tag:v1,v2,...\\n" 并发送
    if (drv_uart_is_tx_free(ctx->bind.huart))
    {
        result = true;

        if (tag != NULL)
        {
            result = _append_str(ctx, max_payload, &idx, tag);
            if (result)
            {
                result = _append_char(ctx, max_payload, &idx, ':');
            }
        }

        for (uint16_t i = 0U; (i < n) && result; i++) // 循环计数器
        {
            char num[20]; // 浮点数转字符串缓存
            result = common_float_to_str(arr[i], num, (uint16_t)sizeof(num));
            if (result)
            {
                result = _append_str(ctx, max_payload, &idx, num);
            }
            if (result && (i != (uint16_t)(n - 1U)))
            {
                result = _append_char(ctx, max_payload, &idx, ',');
            }
        }

        if (result)
        {
            result = _append_char(ctx, max_payload, &idx, '\n');
        }
        if (result)
        {
            result = (drv_uart_send_dma_ex(ctx->bind.huart, (const uint8_t *)ctx->tx_buf, idx) == DRV_UART_OK);
        }
    }

    _tx_unlock(ctx);
    return result;
}

/**
 * @brief 发送整型数组数据帧。
 * @param ctx VOFA 模块上下文指针。
 * @param tag 数据标签（可为 NULL）。
 * @param arr 待发送数组首地址。
 * @param n 数组元素个数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_int_ctx(mod_vofa_ctx_t *ctx, const char *tag, const int32_t *arr, uint16_t n)
{
    bool result = false; // 发送结果
    uint16_t idx = 0U; // 发送缓冲写入索引
    const uint16_t max_payload = (uint16_t)(MOD_VOFA_TX_BUF_SIZE - 1U); // 最大有效负载长度

    // 参数与上下文校验
    if (!_ctx_ready(ctx) || (arr == NULL) || (n == 0U))
    {
        return false;
    }

    // 获取发送锁
    if (!_tx_lock(ctx))
    {
        return false;
    }

    // 串口空闲时组装 "tag:v1,v2,...\\n" 并发送
    if (drv_uart_is_tx_free(ctx->bind.huart))
    {
        result = true;

        if (tag != NULL)
        {
            result = _append_str(ctx, max_payload, &idx, tag);
            if (result)
            {
                result = _append_char(ctx, max_payload, &idx, ':');
            }
        }

        for (uint16_t i = 0U; (i < n) && result; i++) // 循环计数器
        {
            char num[16]; // 整型转字符串缓存
            result = common_int_to_str(arr[i], num, (uint16_t)sizeof(num));
            if (result)
            {
                result = _append_str(ctx, max_payload, &idx, num);
            }
            if (result && (i != (uint16_t)(n - 1U)))
            {
                result = _append_char(ctx, max_payload, &idx, ',');
            }
        }

        if (result)
        {
            result = _append_char(ctx, max_payload, &idx, '\n');
        }
        if (result)
        {
            result = (drv_uart_send_dma_ex(ctx->bind.huart, (const uint8_t *)ctx->tx_buf, idx) == DRV_UART_OK);
        }
    }

    _tx_unlock(ctx);
    return result;
}

/**
 * @brief 发送无符号整型数组数据帧。
 * @param ctx VOFA 模块上下文指针。
 * @param tag 数据标签（可为 NULL）。
 * @param arr 待发送数组首地址。
 * @param n 数组元素个数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_uint_ctx(mod_vofa_ctx_t *ctx, const char *tag, const uint32_t *arr, uint16_t n)
{
    bool result = false; // 发送结果
    uint16_t idx = 0U; // 发送缓冲写入索引
    const uint16_t max_payload = (uint16_t)(MOD_VOFA_TX_BUF_SIZE - 1U); // 最大有效负载长度

    // 参数与上下文校验
    if (!_ctx_ready(ctx) || (arr == NULL) || (n == 0U))
    {
        return false;
    }

    // 获取发送锁
    if (!_tx_lock(ctx))
    {
        return false;
    }

    // 串口空闲时组装 "tag:v1,v2,...\\n" 并发送
    if (drv_uart_is_tx_free(ctx->bind.huart))
    {
        result = true;

        if (tag != NULL)
        {
            result = _append_str(ctx, max_payload, &idx, tag);
            if (result)
            {
                result = _append_char(ctx, max_payload, &idx, ':');
            }
        }

        for (uint16_t i = 0U; (i < n) && result; i++) // 循环计数器
        {
            char num[16]; // 无符号整型转字符串缓存
            result = common_uint_to_str(arr[i], num, (uint16_t)sizeof(num));
            if (result)
            {
                result = _append_str(ctx, max_payload, &idx, num);
            }
            if (result && (i != (uint16_t)(n - 1U)))
            {
                result = _append_char(ctx, max_payload, &idx, ',');
            }
        }

        if (result)
        {
            result = _append_char(ctx, max_payload, &idx, '\n');
        }
        if (result)
        {
            result = (drv_uart_send_dma_ex(ctx->bind.huart, (const uint8_t *)ctx->tx_buf, idx) == DRV_UART_OK);
        }
    }

    _tx_unlock(ctx);
    return result;
}

/**
 * @brief 兼容初始化接口（默认上下文）。
 * @param huart 目标串口句柄。
 * @param sem_id 待添加或移除的信号量句柄。
 * @return 无。
 */
void mod_vofa_init(UART_HandleTypeDef *huart, osSemaphoreId_t sem_id)
{
    mod_vofa_bind_t bind; // 绑定参数
    mod_vofa_ctx_t *ctx = mod_vofa_get_default_ctx(); // 默认上下文

    // 组装默认绑定参数
    memset(&bind, 0, sizeof(bind));
    bind.huart = huart;
    bind.tx_mutex = ctx->bind.tx_mutex;

    if (sem_id != NULL)
    {
        bind.sem_list[0] = sem_id;
        bind.sem_count = 1U;
    }

    // 初始化或重绑定默认上下文
    if (!ctx->inited)
    {
        (void)mod_vofa_ctx_init(ctx, &bind);
    }
    else
    {
        (void)mod_vofa_bind(ctx, &bind);
    }
}

/**
 * @brief 兼容命令读取接口（默认上下文）。
 * @param 无。
 * @return vofa_cmd_id_t 解析得到的命令标识。
 */
vofa_cmd_id_t mod_vofa_get_command(void)
{
    // 从默认上下文读取并清空命令
    return mod_vofa_get_command_ctx(mod_vofa_get_default_ctx());
}

/**
 * @brief 兼容浮点发送接口（默认上下文）。
 * @param tag 数据标签（可为 NULL）。
 * @param arr 待发送数组首地址。
 * @param n 数组元素个数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_float(const char *tag, const float *arr, uint16_t n)
{
    // 默认上下文浮点数组发送封装
    return mod_vofa_send_float_ctx(mod_vofa_get_default_ctx(), tag, arr, n);
}

/**
 * @brief 兼容整型发送接口（默认上下文）。
 * @param tag 数据标签（可为 NULL）。
 * @param arr 待发送数组首地址。
 * @param n 数组元素个数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_int(const char *tag, const int32_t *arr, uint16_t n)
{
    // 默认上下文有符号整型数组发送封装
    return mod_vofa_send_int_ctx(mod_vofa_get_default_ctx(), tag, arr, n);
}

/**
 * @brief 兼容无符号整型发送接口（默认上下文）。
 * @param tag 数据标签（可为 NULL）。
 * @param arr 待发送数组首地址。
 * @param n 数组元素个数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_uint(const char *tag, const uint32_t *arr, uint16_t n)
{
    // 默认上下文无符号整型数组发送封装
    return mod_vofa_send_uint_ctx(mod_vofa_get_default_ctx(), tag, arr, n);
}

/**
 * @brief 兼容字符串发送接口（默认上下文）。
 * @param str 待发送字符串。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_string(const char *str)
{
    // 默认上下文字符串发送封装
    return mod_vofa_send_string_ctx(mod_vofa_get_default_ctx(), str);
}









