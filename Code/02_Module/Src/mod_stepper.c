/**
 * @file mod_stepper.c
 * @brief 步进协议模块实现（仅发送，TX Only）。
 *
 * @details
 * 1. 文件作用：实现步进协议命令组帧、发送状态维护和上下文资源绑定。
 * 2. 解耦边界：仅负责命令发送链路，不处理视觉闭环、轨迹规划或回读解析。
 * 3. 上层绑定：`StepperTask` 负责节拍控制与命令调度，本模块只执行协议发送。
 * 4. 下层依赖：`drv_uart` 执行发送，`mod_uart_guard` 负责 UART 独占仲裁。
 * 5. 生命周期：一个上下文只绑定一个 UART 与驱动地址，支持重复绑定和资源回收。
 */

#include "mod_stepper.h"
#include "mod_uart_guard.h"
#include <string.h>


/**
 * @brief UART索引到上下文的映射表。
 *
 * 用途：
 * 1. 绑定冲突检查（同一个UART只允许一个上下文占用）。
 * 2. 支持 mod_stepper_process(NULL) 时轮询全部实例。
 */
static mod_stepper_ctx_t *s_ctx_by_uart[MOD_STEPPER_MAX_UART_INSTANCES] = {0}; // 模块变量，用于保存运行时状态。

/**
 * @brief 将 UART 实例地址映射为固定索引。
 * @param instance UART外设实例。
 * @return [0..5] 映射成功；-1 表示不支持该实例。
 */
static int8_t _get_uart_index(USART_TypeDef *instance)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    int8_t idx = -1; // 循环或计数变量

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
 * @brief 查询某个上下文当前绑定在哪个 UART 索引。
 * @param ctx 目标上下文。
 * @return [0..5] 找到；-1 未找到或参数非法。
 */
static int8_t _ctx_bound_uart_idx(const mod_stepper_ctx_t *ctx)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if (ctx == NULL)
    {
        return -1;
    }

    for (uint16_t i = 0U; i < MOD_STEPPER_MAX_UART_INSTANCES; i++) // 循环计数器
    {
        if (s_ctx_by_uart[i] == ctx)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

/**
 * @brief 判断上下文是否达到“可发送”状态。
 *
 * 必须同时满足：
 * 1. ctx 非空
 * 2. 已初始化
 * 3. 已绑定
 * 4. huart 有效
 * 5. driver_addr 非 0
 */
/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool _ctx_ready(const mod_stepper_ctx_t *ctx)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return ((ctx != NULL) &&
            ctx->inited &&
            ctx->bound &&
            (ctx->bind.huart != NULL) &&
            (ctx->bind.driver_addr != 0U));
}

/**
 * @brief 获取发送互斥锁（若配置了互斥锁）。
 * @param ctx 上下文。
 * @return true 获取成功或无需加锁；false 获取失败。
 */
static bool _tx_lock(mod_stepper_ctx_t *ctx)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if ((ctx == NULL) || (ctx->bind.tx_mutex == NULL))
    {
        return true;
    }

    return (osMutexAcquire(ctx->bind.tx_mutex, MOD_STEPPER_TX_MUTEX_TIMEOUT_MS) == osOK);
}

/**
 * @brief 释放发送互斥锁（若配置了互斥锁）。
 */
static void _tx_unlock(mod_stepper_ctx_t *ctx)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if ((ctx != NULL) && (ctx->bind.tx_mutex != NULL))
    {
        (void)osMutexRelease(ctx->bind.tx_mutex);
    }
}

/**
 * @brief 刷新 tx_active 状态（轮询式发送完成判定）。
 *
 * 判定逻辑：
 * 1. 若 tx_active=false，直接返回。
 * 2. 若 UART 已空闲，认为本次发送完成，清理 active 并增加成功计数。
 * 3. 若超时仍未空闲，触发超时恢复，清理 active 并增加失败计数。
 *
 * 说明：
 * - 本模块有意不使用 HAL_UART_TxCpltCallback，降低全局回调耦合。
 * - 因此需要在 process() 中轮询硬件状态完成状态推进。
 */
/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 无。
 */
static void _refresh_tx_state(mod_stepper_ctx_t *ctx)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint32_t now; // 局部业务变量
    uint32_t delta; // 局部业务变量

    if (!_ctx_ready(ctx))
    {
        return;
    }

    if (!ctx->tx_active)
    {
        return;
    }

    if (drv_uart_is_tx_free(ctx->bind.huart))
    {
        ctx->tx_active = false;
        ctx->tx_active_tick = 0U;
        ctx->tx_ok_count++;
        return;
    }

    now = HAL_GetTick();
    delta = now - ctx->tx_active_tick;
    if (delta > MOD_STEPPER_TX_ACTIVE_TIMEOUT_MS)
    {
        ctx->tx_active = false;
        ctx->tx_active_tick = 0U;
        ctx->tx_fail_count++;
    }
}

/**
 * @brief 内部统一发送入口（所有协议命令最终都走这里）。
 *
 * 执行步骤：
 * 1. 参数与上下文就绪性检查。
 * 2. 刷新发送状态，避免“上一次发送仍活跃”误发。
 * 3. 检查 UART 是否空闲。
 * 4. 获取可选互斥锁。
 * 5. 拷贝到 tx_buf，启动 DMA 发送。
 * 6. 发送成功则置 active；失败累计失败计数。
 * 7. 释放互斥锁。
 *
 * 返回语义：
 * - true：本次 DMA 已成功启动。
 * - false：未发送（忙、参数错、加锁失败、DMA启动失败等）。
 */
/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param frame 函数输入参数，语义由调用场景决定。
 * @param len 数据长度或数量参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool _send_frame(mod_stepper_ctx_t *ctx, const uint8_t *frame, uint16_t len)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    bool sent = false; // 局部业务变量

    if (!_ctx_ready(ctx) || (frame == NULL) || (len == 0U) || (len > MOD_STEPPER_TX_BUF_SIZE))
    {
        return false;
    }

    _refresh_tx_state(ctx);
    if (ctx->tx_active)
    {
        return false;
    }

    if (!drv_uart_is_tx_free(ctx->bind.huart))
    {
        return false;
    }

    if (!_tx_lock(ctx))
    {
        return false;
    }

    memcpy(ctx->tx_buf, frame, len);
    sent = drv_uart_send_dma(ctx->bind.huart, ctx->tx_buf, len);
    if (sent)
    {
        ctx->tx_active = true;
        ctx->tx_active_tick = HAL_GetTick();
    }
    else
    {
        ctx->tx_fail_count++;
    }

    _tx_unlock(ctx);
    return sent;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param bind 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_ctx_init(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if (ctx == NULL)
    {
        return false;
    }

    /**
     * 若调用者复用旧上下文对象，先解绑，避免UART归属权泄漏。
     */
    if (ctx->inited && ctx->bound)
    {
        mod_stepper_unbind(ctx);
    }

    memset(ctx, 0, sizeof(mod_stepper_ctx_t));
    ctx->inited = true;

    if (bind != NULL)
    {
        return mod_stepper_bind(ctx, bind);
    }

    return true;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param bind 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_init(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return mod_stepper_ctx_init(ctx, bind);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param bind 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_bind(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    int8_t new_idx; // 循环或计数变量
    int8_t old_idx; // 循环或计数变量
    bool claimed = false; // 局部业务变量

    /**
     * 1. 入参校验：
     * - ctx 必须已初始化
     * - bind/huart 必填
     * - driver_addr 不能为 0
     */
    if ((ctx == NULL) || !ctx->inited || (bind == NULL) || (bind->huart == NULL) || (bind->driver_addr == 0U))
    {
        return false;
    }

    /**
     * 2. UART 映射与冲突检查：
     * - 同一 UART 只允许一个上下文绑定（符合“一个串口只绑一个电机”约束）
     */
    new_idx = _get_uart_index(bind->huart->Instance);
    if (new_idx < 0)
    {
        return false;
    }

    if ((s_ctx_by_uart[(uint16_t)new_idx] != NULL) && (s_ctx_by_uart[(uint16_t)new_idx] != ctx))
    {
        return false;
    }

    /**
     * 3. 申请 UART 所有权
     */
    if (!mod_uart_guard_claim(bind->huart, MOD_UART_OWNER_STEPPER))
    {
        return false;
    }
    claimed = true;

    /**
     * 4. 清理旧映射/旧归属（若存在旧绑定）
     */
    old_idx = _ctx_bound_uart_idx(ctx);
    if ((old_idx >= 0) && (s_ctx_by_uart[(uint16_t)old_idx] == ctx))
    {
        s_ctx_by_uart[(uint16_t)old_idx] = NULL;
    }

    if ((old_idx >= 0) && (ctx->bind.huart != NULL) && (ctx->bind.huart != bind->huart))
    {
        (void)mod_uart_guard_release(ctx->bind.huart, MOD_UART_OWNER_STEPPER);
    }

    /**
     * 5. 写入新绑定并复位发送状态
     */
    memset(&ctx->bind, 0, sizeof(ctx->bind));
    ctx->bind.huart = bind->huart;
    ctx->bind.tx_mutex = bind->tx_mutex;
    ctx->bind.driver_addr = bind->driver_addr;
    ctx->bound = true;
    ctx->tx_active = false;
    ctx->tx_active_tick = 0U;
    s_ctx_by_uart[(uint16_t)new_idx] = ctx;

    /**
     * 说明：
     * claimed 仅用于表达“已成功申请过guard”这一事实；
     * 当前分支无失败回滚路径，故仅保持变量占位以便后续扩展。
     */
    (void)claimed;
    return true;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 无。
 */
void mod_stepper_unbind(mod_stepper_ctx_t *ctx)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    int8_t idx; // 循环或计数变量

    if ((ctx == NULL) || !ctx->inited)
    {
        return;
    }

    /**
     * 1. 移除索引映射
     */
    idx = _ctx_bound_uart_idx(ctx);
    if ((idx >= 0) && (s_ctx_by_uart[(uint16_t)idx] == ctx))
    {
        s_ctx_by_uart[(uint16_t)idx] = NULL;
    }

    /**
     * 2. 释放 UART 所有权
     */
    if (ctx->bind.huart != NULL)
    {
        (void)mod_uart_guard_release(ctx->bind.huart, MOD_UART_OWNER_STEPPER);
    }

    /**
     * 3. 清理绑定状态
     */
    memset(&ctx->bind, 0, sizeof(ctx->bind));
    ctx->bound = false;
    ctx->tx_active = false;
    ctx->tx_active_tick = 0U;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_is_bound(const mod_stepper_ctx_t *ctx)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return _ctx_ready(ctx);
}

const mod_stepper_bind_t *mod_stepper_get_bind(const mod_stepper_ctx_t *ctx)
{
    if ((ctx == NULL) || !ctx->inited)
    {
        return NULL;
    }
    return &ctx->bind;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @return 无。
 */
void mod_stepper_process(mod_stepper_ctx_t *ctx)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    /**
     * 模式A：处理单个实例
     */
    if (ctx != NULL)
    {
        _refresh_tx_state(ctx);
        return;
    }

    /**
     * 模式B：处理全部实例
     */
    for (uint16_t i = 0U; i < MOD_STEPPER_MAX_UART_INSTANCES; i++) // 循环计数器
    {
        _refresh_tx_state(s_ctx_by_uart[i]);
    }
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param buf 输入/输出缓冲区指针。
 * @param len 数据长度或数量参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_send_raw(mod_stepper_ctx_t *ctx, const uint8_t *buf, uint16_t len)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return _send_frame(ctx, buf, len);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param enable 状态或模式控制参数。
 * @param sync_flag 状态或模式控制参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_enable(mod_stepper_ctx_t *ctx, bool enable, bool sync_flag)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint8_t frame[6]; // 使能命令帧缓存

    if (!_ctx_ready(ctx))
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0xF3U;
    frame[2] = 0xABU;
    frame[3] = (uint8_t)enable;
    frame[4] = (uint8_t)sync_flag;
    frame[5] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame(ctx, frame, 6U);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param dir 函数输入参数，语义由调用场景决定。
 * @param vel_rpm 函数输入参数，语义由调用场景决定。
 * @param acc 函数输入参数，语义由调用场景决定。
 * @param sync_flag 状态或模式控制参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_velocity(mod_stepper_ctx_t *ctx, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint8_t frame[8]; // 速度命令帧缓存

    if (!_ctx_ready(ctx))
    {
        return false;
    }

    /**
     * 速度安全限幅
     */
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
    return _send_frame(ctx, frame, 8U);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param dir 函数输入参数，语义由调用场景决定。
 * @param vel_rpm 函数输入参数，语义由调用场景决定。
 * @param acc 函数输入参数，语义由调用场景决定。
 * @param pulse 函数输入参数，语义由调用场景决定。
 * @param absolute_mode 状态或模式控制参数。
 * @param sync_flag 状态或模式控制参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_position(mod_stepper_ctx_t *ctx, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint8_t frame[13]; // 位置命令帧缓存

    if (!_ctx_ready(ctx))
    {
        return false;
    }

    /**
     * 速度安全限幅
     */
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
    return _send_frame(ctx, frame, 13U);
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param ctx 模块上下文对象，用于保存运行时状态。
 * @param sync_flag 状态或模式控制参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_stepper_stop(mod_stepper_ctx_t *ctx, bool sync_flag)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint8_t frame[5]; // 停止命令帧缓存

    if (!_ctx_ready(ctx))
    {
        return false;
    }

    frame[0] = ctx->bind.driver_addr;
    frame[1] = 0xFEU;
    frame[2] = 0x98U;
    frame[3] = (uint8_t)sync_flag;
    frame[4] = MOD_STEPPER_FRAME_TAIL;
    return _send_frame(ctx, frame, 5U);
}



