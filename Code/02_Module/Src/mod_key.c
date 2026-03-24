/**
 * @file    mod_key.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   按键模块实现（ctx 架构）。
 * @details
 * 1. 文件作用：实现按键上下文生命周期、映射绑定和驱动事件语义映射。
 * 2. 解耦边界：模块层不做任务调度，只输出可复用按键事件。
 * 3. 下层协作：通过 `drv_key` 做时序状态机，通过 `drv_gpio` 回调读取电平。
 */

#include "mod_key.h"

#include "drv_key.h"

#include <string.h>

static mod_key_ctx_t s_default_ctx; // s_default_ctx：默认按键上下文。
static mod_key_ctx_t *s_active_ctx; // s_active_ctx：当前挂载到 drv_key 的活动上下文。

/**
 * @brief 毫秒转换为扫描 tick（向上取整，最小 1）。
 * @param ms 毫秒值。
 * @return uint16_t tick 数。
 */
static uint16_t mod_key_ms_to_ticks(uint16_t ms)
{
    uint16_t ticks = 1U; // ticks：换算后的 tick 数。

    // 步骤1：扫描周期异常时返回最小 tick，避免除零。
    if (MOD_KEY_SCAN_PERIOD_MS == 0U)
    {
        return 1U;
    }

    // 步骤2：向上取整换算 ms -> tick。
    ticks = (uint16_t)((ms + MOD_KEY_SCAN_PERIOD_MS - 1U) / MOD_KEY_SCAN_PERIOD_MS);

    // 步骤3：保证返回值最小为 1。
    if (ticks == 0U)
    {
        ticks = 1U;
    }

    return ticks;
}

/**
 * @brief 判断上下文是否可用于扫描。
 * @param ctx 目标上下文。
 * @return true 可用。
 * @return false 不可用。
 */
static bool mod_key_ctx_ready(const mod_key_ctx_t *ctx)
{
    return ((ctx != NULL) && ctx->inited && ctx->bound && (ctx == s_active_ctx));
}

/**
 * @brief 校验单个按键映射项。
 * @param item 映射项指针。
 * @return true 合法。
 * @return false 非法。
 */
static bool mod_key_check_item(const mod_key_hw_cfg_t *item)
{
    // 步骤1：基础指针和端口句柄必须有效。
    if ((item == NULL) || (item->port == NULL))
    {
        return false;
    }

    // 步骤2：按下有效电平必须是合法枚举值。
    if ((item->active_level != GPIO_LEVEL_LOW) && (item->active_level != GPIO_LEVEL_HIGH))
    {
        return false;
    }

    // 步骤3：语义映射必须完整，避免事件丢失。
    if ((item->click_event == MOD_KEY_EVENT_NONE) ||
        (item->double_event == MOD_KEY_EVENT_NONE) ||
        (item->long_event == MOD_KEY_EVENT_NONE))
    {
        return false;
    }

    return true;
}

/**
 * @brief 提供给 drv_key 的按键电平读取回调。
 * @param key_id 按键索引。
 * @param user_arg 模块上下文指针。
 * @return true 按下。
 * @return false 未按下或参数非法。
 */
static bool mod_key_read_cb(uint8_t key_id, void *user_arg)
{
    mod_key_ctx_t *ctx = (mod_key_ctx_t *)user_arg; // ctx：回调绑定的模块上下文。
    const mod_key_hw_cfg_t *item = NULL; // item：当前 key_id 对应映射项。
    gpio_level_e level = GPIO_LEVEL_LOW; // level：GPIO 当前采样电平。

    // 步骤1：上下文和 key_id 合法性校验。
    if (!mod_key_ctx_ready(ctx) || (key_id >= ctx->key_num))
    {
        return false;
    }

    // 步骤2：读取硬件电平并按 active_level 判断按下态。
    item = &ctx->map[key_id];
    level = drv_gpio_read(item->port, item->pin);

    return (level == item->active_level);
}

mod_key_ctx_t *mod_key_get_default_ctx(void)
{
    return &s_default_ctx;
}

bool mod_key_ctx_init(mod_key_ctx_t *ctx, const mod_key_bind_t *bind)
{
    // 步骤1：参数校验。
    if (ctx == NULL)
    {
        return false;
    }

    // 步骤2：清空历史状态并标记初始化完成。
    memset(ctx, 0, sizeof(*ctx));
    ctx->inited = true;

    // 步骤3：支持“初始化即绑定”。
    if (bind != NULL)
    {
        return mod_key_bind(ctx, bind);
    }

    return true;
}

void mod_key_ctx_deinit(mod_key_ctx_t *ctx)
{
    // 步骤1：空指针保护。
    if (ctx == NULL)
    {
        return;
    }

    // 步骤2：先解绑再清零。
    mod_key_unbind(ctx);
    memset(ctx, 0, sizeof(*ctx));
}

bool mod_key_bind(mod_key_ctx_t *ctx, const mod_key_bind_t *bind)
{
    drv_key_cfg_t cfg; // cfg：传给 drv_key 的初始化参数。

    // 步骤1：参数与上下文状态校验。
    if ((ctx == NULL) || (!ctx->inited) || (bind == NULL) || (bind->map == NULL) ||
        (bind->key_num == 0U) || (bind->key_num > MOD_KEY_MAX_NUM))
    {
        return false;
    }

    // 步骤2：drv_key 为单实例驱动，同一时刻只允许一个活动 ctx。
    if ((s_active_ctx != NULL) && (s_active_ctx != ctx))
    {
        return false;
    }

    // 步骤3：逐项校验映射表。
    for (uint8_t i = 0U; i < bind->key_num; i++)
    {
        if (!mod_key_check_item(&bind->map[i]))
        {
            return false;
        }
    }

    // 步骤4：写入映射副本与 key 数量。
    memset(ctx->map, 0, sizeof(ctx->map));
    memcpy(ctx->map, bind->map, (uint32_t)bind->key_num * sizeof(mod_key_hw_cfg_t));
    ctx->key_num = bind->key_num;

    // 步骤5：构造驱动层配置并初始化 drv_key。
    memset(&cfg, 0, sizeof(cfg));
    cfg.key_num = ctx->key_num;
    cfg.debounce_ticks = mod_key_ms_to_ticks(MOD_KEY_DEBOUNCE_MS);
    cfg.long_press_ticks = mod_key_ms_to_ticks(MOD_KEY_LONG_PRESS_MS);
    cfg.double_click_ticks = mod_key_ms_to_ticks(MOD_KEY_DOUBLE_CLICK_MS);
    cfg.read_cb = mod_key_read_cb;
    cfg.user_arg = ctx;

    if (!drv_key_init(&cfg))
    {
        // 步骤5.1：驱动初始化失败时回滚上下文状态。
        memset(ctx->map, 0, sizeof(ctx->map));
        ctx->key_num = 0U;
        ctx->bound = false;
        return false;
    }

    // 步骤6：注册活动上下文并标记绑定完成。
    s_active_ctx = ctx;
    ctx->bound = true;

    return true;
}

void mod_key_unbind(mod_key_ctx_t *ctx)
{
    // 步骤1：仅对已初始化上下文执行解绑。
    if ((ctx == NULL) || (!ctx->inited))
    {
        return;
    }

    // 步骤2：若解绑对象是活动上下文，先释放活动绑定。
    if (s_active_ctx == ctx)
    {
        s_active_ctx = NULL;
    }

    // 步骤3：清空映射与状态。
    memset(ctx->map, 0, sizeof(ctx->map));
    ctx->key_num = 0U;
    ctx->bound = false;
}

bool mod_key_is_bound(const mod_key_ctx_t *ctx)
{
    return mod_key_ctx_ready(ctx);
}

mod_key_event_e mod_key_scan(mod_key_ctx_t *ctx)
{
    drv_key_event_t drv_evt; // drv_evt：驱动层扫描输出事件。
    const mod_key_hw_cfg_t *item = NULL; // item：事件来源按键的映射配置。

    // 步骤1：上下文就绪校验。
    if (!mod_key_ctx_ready(ctx))
    {
        return MOD_KEY_EVENT_NONE;
    }

    // 步骤2：推进驱动状态机并获取事件。
    if (!drv_key_scan(&drv_evt))
    {
        return MOD_KEY_EVENT_NONE;
    }

    // 步骤3：无事件或 key_id 越界直接返回 NONE。
    if ((drv_evt.type == DRV_KEY_EVENT_NONE) || (drv_evt.key_id >= ctx->key_num))
    {
        return MOD_KEY_EVENT_NONE;
    }

    // 步骤4：按映射表完成驱动事件 -> 模块事件转换。
    item = &ctx->map[drv_evt.key_id];
    switch (drv_evt.type)
    {
    case DRV_KEY_EVENT_CLICK:
        return item->click_event;

    case DRV_KEY_EVENT_DOUBLE_CLICK:
        return item->double_event;

    case DRV_KEY_EVENT_LONG_PRESS:
        return item->long_event;

    case DRV_KEY_EVENT_NONE:
    default:
        return MOD_KEY_EVENT_NONE;
    }
}
