/**
 * @file mod_vofa.c
 * @brief VOFA 通信模块实现
 * @details
 * 1. 文件作用：实现 VOFA 通道上下文管理、命令解析和多类型数据打包发送。
 * 2. 解耦边界：协议处理在模块内部闭环，不直接执行上层业务命令动作。
 * 3. 上层绑定：`InitTask` 注入串口与互斥资源，业务任务按需调用发送/读命令接口。
 * 4. 下层依赖：`drv_uart` 提供 DMA/阻塞收发，`mod_uart_guard` 负责 UART 独占仲裁。
 * 5. 生命周期：先 `ctx_init/bind`，运行期 `process` 驱动协议状态，最后按需解绑。
 */

#include "mod_vofa.h"


typedef struct
{
    const char *cmd_str; // 命令文本
    vofa_cmd_id_t cmd_id; // 对应的命令ID
} vofa_cmd_entry_t;

static mod_vofa_ctx_t s_default_ctx; // 默认上下文实例
static mod_vofa_ctx_t *s_active_ctx = NULL; // 当前激活上下文

static const vofa_cmd_entry_t s_cmd_table[] =
{
    {"start", VOFA_CMD_START},
    {"stop",  VOFA_CMD_STOP}
};

#define CMD_TABLE_SIZE (sizeof(s_cmd_table) / sizeof(s_cmd_table[0]))

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param 无。
 * @return 返回计算结果或状态码，具体语义见实现。
 */
static uint32_t _critical_enter(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint32_t primask = __get_PRIMASK(); // 进入前中断屏蔽状态
    __disable_irq();
    return primask;
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param primask 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
static void _critical_exit(uint32_t primask)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    __set_PRIMASK(primask);
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool _ctx_ready(const mod_vofa_ctx_t *ctx)
{
    //1. 判定上下文是否已初始化、已绑定且 UART 有效
    return ((ctx != NULL) &&
            ctx->inited &&
            ctx->bound &&
            (ctx->bind.huart != NULL));
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param bind 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
static void _copy_bind_cfg(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind)
{
    //1. 复制 UART、互斥锁与信号量绑定配置
    ctx->bind.huart = bind->huart;
    ctx->bind.tx_mutex = bind->tx_mutex;
    ctx->bind.sem_count = 0U;
    memset(ctx->bind.sem_list, 0, sizeof(ctx->bind.sem_list));

    //2. 仅复制有效信号量句柄，并限制最大数量
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
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 无。
 */
static void _notify_all_semaphores(const mod_vofa_ctx_t *ctx)
{
    //1. 逐个释放绑定信号量，通知上层有新命令
    for (uint8_t i = 0U; i < ctx->bind.sem_count; i++) // 循环计数器
    {
        if (ctx->bind.sem_list[i] != NULL)
        {
            (void)osSemaphoreRelease(ctx->bind.sem_list[i]);
        }
    }
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 无。
 */
static void _restart_rx_dma(mod_vofa_ctx_t *ctx)
{
    //1. 上下文就绪时重启 DMA 接收，失败执行停启重试
    if (!_ctx_ready(ctx))
    {
        return;
    }

    if (!drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_buf, MOD_VOFA_RX_BUF_SIZE))
    {
        drv_uart_receive_dma_stop(ctx->bind.huart);
        (void)drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_buf, MOD_VOFA_RX_BUF_SIZE);
    }
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param max 函数输入参数，语义由调用场景决定。
 * @param p_idx 输入/输出缓冲区指针。
 * @param c 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool _append_char(mod_vofa_ctx_t *ctx, uint16_t max, uint16_t *p_idx, char c)
{
    //1. 参数与缓冲区边界校验
    if ((ctx == NULL) || (p_idx == NULL) || (*p_idx >= max))
    {
        return false;
    }

    //2. 追加单字符并更新写入索引
    ctx->tx_buf[*p_idx] = c;
    (*p_idx)++;
    return true;
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param max 函数输入参数，语义由调用场景决定。
 * @param p_idx 输入/输出缓冲区指针。
 * @param s 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool _append_str(mod_vofa_ctx_t *ctx, uint16_t max, uint16_t *p_idx, const char *s)
{
    bool result = true; // 追加结果

    //1. 空字符串视为追加成功
    if (s == NULL)
    {
        return true;
    }

    //2. 逐字符追加，任一步失败则终止
    while ((*s != '\0') && result)
    {
        result = _append_char(ctx, max, p_idx, *s);
        s++;
    }

    return result;
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool _tx_lock(mod_vofa_ctx_t *ctx)
{
    //1. 未配置互斥锁时默认加锁成功
    if (ctx->bind.tx_mutex == NULL)
    {
        return true;
    }

    return (osMutexAcquire(ctx->bind.tx_mutex, MOD_VOFA_TX_MUTEX_TIMEOUT_MS) == osOK);
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 无。
 */
static void _tx_unlock(mod_vofa_ctx_t *ctx)
{
    //1. 已配置互斥锁时释放发送锁
    if (ctx->bind.tx_mutex != NULL)
    {
        (void)osMutexRelease(ctx->bind.tx_mutex);
    }
}

/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param len 数据长度或数量参数。
 * @return 无。
 */
static void _vofa_rx_callback_handler(uint16_t len)
{
    mod_vofa_ctx_t *ctx = s_active_ctx; // 当前激活上下文

    //1. 上下文校验
    if (!_ctx_ready(ctx))
    {
        return;
    }

    //2. 接收长度限幅保护
    if (len > MOD_VOFA_RX_BUF_SIZE)
    {
        len = MOD_VOFA_RX_BUF_SIZE;
    }

    //3. 解析命令文本，匹配成功则更新命令并通知上层
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

                //3.1 匹配到命令后重置 DMA 缓冲并立即重启接收
                HAL_UART_DMAStop(ctx->bind.huart);
                memset(ctx->rx_buf, 0, MOD_VOFA_RX_BUF_SIZE);
                _restart_rx_dma(ctx);
                return;
            }
        }
    }

    //4. 未匹配命令时继续重启 DMA 接收
    _restart_rx_dma(ctx);
}

mod_vofa_ctx_t *mod_vofa_get_default_ctx(void)
{
    return &s_default_ctx;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param bind 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_ctx_init(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind)
{
    //1. 参数校验
    if (ctx == NULL)
    {
        return false;
    }

    //2. 清空上下文并标记初始化完成
    memset(ctx, 0, sizeof(mod_vofa_ctx_t));
    ctx->inited = true;
    ctx->last_cmd = VOFA_CMD_NONE;

    //3. 若提供绑定参数则直接执行绑定
    if (bind != NULL)
    {
        return mod_vofa_bind(ctx, bind);
    }

    return true;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param bind 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_bind(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind)
{
    bool result = false; // 绑定结果

    //1. 参数与初始化状态校验
    if ((ctx == NULL) || !ctx->inited || (bind == NULL) || (bind->huart == NULL))
    {
        return false;
    }

    //2. 已绑定则先解绑旧资源
    if (ctx->bound)
    {
        mod_vofa_unbind(ctx);
    }

    //3. 申请 UART 资源所有权
    if (!mod_uart_guard_claim(bind->huart, MOD_UART_OWNER_VOFA))
    {
        return false;
    }

    //4. 复制绑定配置并清理收发缓存
    _copy_bind_cfg(ctx, bind);
    ctx->last_cmd = VOFA_CMD_NONE;
    memset(ctx->rx_buf, 0, sizeof(ctx->rx_buf));
    memset(ctx->tx_buf, 0, sizeof(ctx->tx_buf));

    //5. 注册回调并启动 DMA 接收
    s_active_ctx = ctx;
    result = drv_uart_register_callback(ctx->bind.huart, _vofa_rx_callback_handler);
    if (result)
    {
        result = drv_uart_receive_dma_start(ctx->bind.huart, ctx->rx_buf, MOD_VOFA_RX_BUF_SIZE);
    }

    //6. 失败回滚资源，成功更新 bound 状态
    if (!result)
    {
        (void)drv_uart_register_callback(ctx->bind.huart, NULL);
        (void)mod_uart_guard_release(ctx->bind.huart, MOD_UART_OWNER_VOFA);
        if (s_active_ctx == ctx)
        {
            s_active_ctx = NULL;
        }
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
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 无。
 */
void mod_vofa_unbind(mod_vofa_ctx_t *ctx)
{
    //1. 参数与状态校验
    if ((ctx == NULL) || !ctx->inited || !ctx->bound || (ctx->bind.huart == NULL))
    {
        return;
    }

    //2. 停止 DMA、注销回调并释放 UART 资源
    drv_uart_receive_dma_stop(ctx->bind.huart);
    (void)drv_uart_register_callback(ctx->bind.huart, NULL);
    (void)mod_uart_guard_release(ctx->bind.huart, MOD_UART_OWNER_VOFA);

    //3. 清理活动上下文和绑定状态
    if (s_active_ctx == ctx)
    {
        s_active_ctx = NULL;
    }

    memset(&ctx->bind, 0, sizeof(ctx->bind));
    memset(ctx->rx_buf, 0, sizeof(ctx->rx_buf));
    ctx->bound = false;
    ctx->last_cmd = VOFA_CMD_NONE;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_is_bound(const mod_vofa_ctx_t *ctx)
{
    //1. 返回上下文是否处于可工作绑定状态
    return _ctx_ready(ctx);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param sem_id 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_add_semaphore(mod_vofa_ctx_t *ctx, osSemaphoreId_t sem_id)
{
    bool result = false; // 添加结果
    uint32_t primask; // 临界区状态保存值

    //1. 参数与上下文校验
    if (!_ctx_ready(ctx) || (sem_id == NULL))
    {
        return false;
    }

    //2. 临界区内：已存在则成功，不存在则尝试追加
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
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param sem_id 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_remove_semaphore(mod_vofa_ctx_t *ctx, osSemaphoreId_t sem_id)
{
    bool result = false; // 删除结果
    uint32_t primask; // 临界区状态保存值

    //1. 参数与上下文校验
    if (!_ctx_ready(ctx) || (sem_id == NULL))
    {
        return false;
    }

    //2. 临界区内查找并删除，后续元素前移
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
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 无。
 */
void mod_vofa_clear_semaphores(mod_vofa_ctx_t *ctx)
{
    uint32_t primask; // 临界区状态保存值

    //1. 上下文校验
    if (!_ctx_ready(ctx))
    {
        return;
    }

    //2. 清空信号量列表和计数
    primask = _critical_enter();
    memset(ctx->bind.sem_list, 0, sizeof(ctx->bind.sem_list));
    ctx->bind.sem_count = 0U;
    _critical_exit(primask);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param mutex_id 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
void mod_vofa_set_tx_mutex(mod_vofa_ctx_t *ctx, osMutexId_t mutex_id)
{
    uint32_t primask; // 临界区状态保存值

    //1. 参数与初始化状态校验
    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    //2. 原子更新互斥锁句柄
    primask = _critical_enter();
    ctx->bind.tx_mutex = mutex_id;
    _critical_exit(primask);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 返回函数执行结果。
 */
vofa_cmd_id_t mod_vofa_get_command_ctx(mod_vofa_ctx_t *ctx)
{
    vofa_cmd_id_t cmd = VOFA_CMD_NONE; // 返回命令
    uint32_t primask; // 临界区状态保存值

    //1. 参数与初始化状态校验
    if ((ctx == NULL) || !ctx->inited)
    {
        return VOFA_CMD_NONE;
    }

    //2. 读取并清空“最后命令”字段
    primask = _critical_enter();
    cmd = ctx->last_cmd;
    ctx->last_cmd = VOFA_CMD_NONE;
    _critical_exit(primask);

    return cmd;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param str 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_string_ctx(mod_vofa_ctx_t *ctx, const char *str)
{
    bool result = false; // 发送结果
    uint16_t idx = 0U; // 发送缓冲写入索引
    const uint16_t max_payload = (uint16_t)(MOD_VOFA_TX_BUF_SIZE - 1U); // 最大有效负载长度

    //1. 参数与上下文校验
    if (!_ctx_ready(ctx) || (str == NULL))
    {
        return false;
    }

    //2. 获取发送锁
    if (!_tx_lock(ctx))
    {
        return false;
    }

    //3. 串口空闲时组包并 DMA 发送
    if (drv_uart_is_tx_free(ctx->bind.huart))
    {
        result = _append_str(ctx, max_payload, &idx, str);
        if (result)
        {
            result = _append_char(ctx, max_payload, &idx, '\n');
        }
        if (result)
        {
            result = drv_uart_send_dma(ctx->bind.huart, (uint8_t *)ctx->tx_buf, idx);
        }
    }

    _tx_unlock(ctx);
    return result;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param tag 函数输入参数，语义由调用场景决定。
 * @param arr 函数输入参数，语义由调用场景决定。
 * @param n 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_float_ctx(mod_vofa_ctx_t *ctx, const char *tag, const float *arr, uint16_t n)
{
    bool result = false; // 发送结果
    uint16_t idx = 0U; // 发送缓冲写入索引
    const uint16_t max_payload = (uint16_t)(MOD_VOFA_TX_BUF_SIZE - 1U); // 最大有效负载长度

    //1. 参数与上下文校验
    if (!_ctx_ready(ctx) || (arr == NULL) || (n == 0U))
    {
        return false;
    }

    //2. 获取发送锁
    if (!_tx_lock(ctx))
    {
        return false;
    }

    //3. 串口空闲时组装 "tag:v1,v2,...\\n" 并发送
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
            result = drv_uart_send_dma(ctx->bind.huart, (uint8_t *)ctx->tx_buf, idx);
        }
    }

    _tx_unlock(ctx);
    return result;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param tag 函数输入参数，语义由调用场景决定。
 * @param arr 函数输入参数，语义由调用场景决定。
 * @param n 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_int_ctx(mod_vofa_ctx_t *ctx, const char *tag, const int32_t *arr, uint16_t n)
{
    bool result = false; // 发送结果
    uint16_t idx = 0U; // 发送缓冲写入索引
    const uint16_t max_payload = (uint16_t)(MOD_VOFA_TX_BUF_SIZE - 1U); // 最大有效负载长度

    //1. 参数与上下文校验
    if (!_ctx_ready(ctx) || (arr == NULL) || (n == 0U))
    {
        return false;
    }

    //2. 获取发送锁
    if (!_tx_lock(ctx))
    {
        return false;
    }

    //3. 串口空闲时组装 "tag:v1,v2,...\\n" 并发送
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
            result = drv_uart_send_dma(ctx->bind.huart, (uint8_t *)ctx->tx_buf, idx);
        }
    }

    _tx_unlock(ctx);
    return result;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param tag 函数输入参数，语义由调用场景决定。
 * @param arr 函数输入参数，语义由调用场景决定。
 * @param n 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_uint_ctx(mod_vofa_ctx_t *ctx, const char *tag, const uint32_t *arr, uint16_t n)
{
    bool result = false; // 发送结果
    uint16_t idx = 0U; // 发送缓冲写入索引
    const uint16_t max_payload = (uint16_t)(MOD_VOFA_TX_BUF_SIZE - 1U); // 最大有效负载长度

    //1. 参数与上下文校验
    if (!_ctx_ready(ctx) || (arr == NULL) || (n == 0U))
    {
        return false;
    }

    //2. 获取发送锁
    if (!_tx_lock(ctx))
    {
        return false;
    }

    //3. 串口空闲时组装 "tag:v1,v2,...\\n" 并发送
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
            result = drv_uart_send_dma(ctx->bind.huart, (uint8_t *)ctx->tx_buf, idx);
        }
    }

    _tx_unlock(ctx);
    return result;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param huart 函数输入参数，语义由调用场景决定。
 * @param sem_id 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
void mod_vofa_init(UART_HandleTypeDef *huart, osSemaphoreId_t sem_id)
{
    mod_vofa_bind_t bind; // 绑定参数
    mod_vofa_ctx_t *ctx = mod_vofa_get_default_ctx(); // 默认上下文

    //1. 组装默认绑定参数
    memset(&bind, 0, sizeof(bind));
    bind.huart = huart;
    bind.tx_mutex = ctx->bind.tx_mutex;

    if (sem_id != NULL)
    {
        bind.sem_list[0] = sem_id;
        bind.sem_count = 1U;
    }

    //2. 初始化或重绑定默认上下文
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
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 返回函数执行结果。
 */
vofa_cmd_id_t mod_vofa_get_command(void)
{
    //1. 从默认上下文读取并清空命令
    return mod_vofa_get_command_ctx(mod_vofa_get_default_ctx());
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param tag 函数输入参数，语义由调用场景决定。
 * @param arr 函数输入参数，语义由调用场景决定。
 * @param n 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_float(const char *tag, const float *arr, uint16_t n)
{
    //1. 默认上下文浮点数组发送封装
    return mod_vofa_send_float_ctx(mod_vofa_get_default_ctx(), tag, arr, n);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param tag 函数输入参数，语义由调用场景决定。
 * @param arr 函数输入参数，语义由调用场景决定。
 * @param n 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_int(const char *tag, const int32_t *arr, uint16_t n)
{
    //1. 默认上下文有符号整型数组发送封装
    return mod_vofa_send_int_ctx(mod_vofa_get_default_ctx(), tag, arr, n);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param tag 函数输入参数，语义由调用场景决定。
 * @param arr 函数输入参数，语义由调用场景决定。
 * @param n 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_uint(const char *tag, const uint32_t *arr, uint16_t n)
{
    //1. 默认上下文无符号整型数组发送封装
    return mod_vofa_send_uint_ctx(mod_vofa_get_default_ctx(), tag, arr, n);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param str 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_vofa_send_string(const char *str)
{
    //1. 默认上下文字符串发送封装
    return mod_vofa_send_string_ctx(mod_vofa_get_default_ctx(), str);
}
