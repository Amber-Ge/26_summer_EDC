/**
 * @file    drv_key.c
 * @brief   通用按键状态机驱动实现。
 * @details
 * 1. 文件作用：实现按键消抖、单击/双击/长按事件判定状态机。
 * 2. 解耦边界：仅处理时间阈值和状态迁移，不绑定具体 GPIO 与业务语义。
 * 3. 上层绑定：`mod_key` 注入读取回调并按扫描周期驱动状态机。
 * 4. 下层依赖：仅依赖基础类型与时间参数，不依赖 HAL 层。
 */

#include "drv_key.h"
#include <string.h>

/**
 * @brief 按键状态机状态定义
 */
typedef enum
{
    DRV_KEY_ST_IDLE = 0U,               // 空闲态：等待第一次按下
    DRV_KEY_ST_PRESS_DEBOUNCE,          // 第一次按下消抖态
    DRV_KEY_ST_PRESSED,                 // 第一次按下保持态（在此判断长按）
    DRV_KEY_ST_RELEASE_DEBOUNCE,        // 第一次释放消抖态
    DRV_KEY_ST_WAIT_SECOND_CLICK,       // 等待第二次点击窗口
    DRV_KEY_ST_SECOND_PRESS_DEBOUNCE,   // 第二次按下消抖态
    DRV_KEY_ST_SECOND_PRESSED,          // 第二次按下保持态
    DRV_KEY_ST_SECOND_RELEASE_DEBOUNCE  // 第二次释放消抖态
} drv_key_state_e;

/**
 * @brief 单个按键运行对象
 */
typedef struct
{
    drv_key_state_e state;          // 当前状态机状态
    uint16_t debounce_cnt;          // 消抖计数
    uint16_t press_tick_cnt;        // 按住计时（用于长按判定）
    uint16_t wait_second_tick_cnt;  // 双击窗口计时
    bool long_press_reported;        // 本次按压是否已上报长按
} drv_key_obj_t;

/* ============================ 驱动私有全局变量 ============================ */

static drv_key_cfg_t s_drv_key_cfg;                                       // 驱动配置缓存
static drv_key_obj_t s_drv_key_objs[DRV_KEY_MAX_NUM];                     // 每个按键的状态对象
static bool s_drv_key_inited = false;                                     // 驱动是否已初始化
static drv_key_event_t s_drv_key_pending_evt = {0U, DRV_KEY_EVENT_NONE};  // 待上报事件缓存

/* ============================ 驱动私有工具函数 ============================ */

/**
 * @brief 设置待上报事件（只保留一个事件槽）
 * @param key_id 按键索引
 * @param type   事件类型
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param type 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
static void drv_key_set_pending_event(uint8_t key_id, drv_key_event_type_e type)
{
    // 1. 只有当前 pending 为空时才写入，避免覆盖未被上层取走的事件。
    if (s_drv_key_pending_evt.type == DRV_KEY_EVENT_NONE)
    {
        s_drv_key_pending_evt.key_id = key_id;
        s_drv_key_pending_evt.type = type;
    }
}

/**
 * @brief 读取某个按键是否处于按下态
 * @param key_id 按键索引
 * @return true 按下
 * @return false 未按下或读取失败
 */
static bool drv_key_is_pressed(uint8_t key_id)
{
    bool pressed = false; // 默认返回未按下

    // 1. 如果回调为空，直接返回 false，防止空指针调用。
    if (s_drv_key_cfg.read_cb == NULL)
    {
        return false;
    }

    // 2. 调用上层回调读取当前 key_id 的电平状态。
    pressed = s_drv_key_cfg.read_cb(key_id, s_drv_key_cfg.user_arg);

    // 3. 返回回调读取结果。
    return pressed;
}

/**
 * @brief 把按键对象内部计数器复位到初始状态
 * @param key_obj 按键对象
 */
static void drv_key_reset_key_runtime(drv_key_obj_t *key_obj)
{
    // 1. 参数保护：对象为空时直接返回。
    if (key_obj == NULL)
    {
        return;
    }

    // 2. 清空消抖计数。
    key_obj->debounce_cnt = 0U;

    // 3. 清空按住计时。
    key_obj->press_tick_cnt = 0U;

    // 4. 清空双击窗口计时。
    key_obj->wait_second_tick_cnt = 0U;

    // 5. 清空“长按已上报”标志。
    key_obj->long_press_reported = false;
}

/**
 * @brief 处理空闲态
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param key_obj 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static drv_key_state_e drv_key_handle_idle(uint8_t key_id, drv_key_obj_t *key_obj)
{
    drv_key_state_e next_state = DRV_KEY_ST_IDLE; // 默认保持空闲

    // 1. 参数保护：对象为空则保持空闲。
    if (key_obj == NULL)
    {
        return next_state;
    }

    // 2. 只要检测到按下，就进入第一次按下消抖态。
    if (drv_key_is_pressed(key_id))
    {
        key_obj->debounce_cnt = 0U;
        next_state = DRV_KEY_ST_PRESS_DEBOUNCE;
    }

    // 3. 返回下一状态。
    return next_state;
}

/**
 * @brief 处理第一次按下消抖态
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param key_obj 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static drv_key_state_e drv_key_handle_press_debounce(uint8_t key_id, drv_key_obj_t *key_obj)
{
    drv_key_state_e next_state = DRV_KEY_ST_PRESS_DEBOUNCE; // 默认保持该状态

    // 1. 参数保护：对象为空时回空闲。
    if (key_obj == NULL)
    {
        return DRV_KEY_ST_IDLE;
    }

    // 2. 如果电平持续保持按下，则累计消抖计数。
    if (drv_key_is_pressed(key_id))
    {
        key_obj->debounce_cnt++;

        // 3. 达到消抖阈值，判定第一次按下成立，进入按下保持态。
        if (key_obj->debounce_cnt >= s_drv_key_cfg.debounce_ticks)
        {
            key_obj->press_tick_cnt = 0U;
            key_obj->long_press_reported = false;
            next_state = DRV_KEY_ST_PRESSED;
        }
    }
    else
    {
        // 4. 中途松开则视为抖动，回到空闲。
        next_state = DRV_KEY_ST_IDLE;
    }

    // 5. 返回下一状态。
    return next_state;
}

/**
 * @brief 处理第一次按下保持态（在此判断长按）
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param key_obj 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static drv_key_state_e drv_key_handle_pressed(uint8_t key_id, drv_key_obj_t *key_obj)
{
    drv_key_state_e next_state = DRV_KEY_ST_PRESSED; // 默认保持按下态

    // 1. 参数保护：对象为空时回空闲。
    if (key_obj == NULL)
    {
        return DRV_KEY_ST_IDLE;
    }

    // 2. 如果当前仍然按下，则推进长按计时。
    if (drv_key_is_pressed(key_id))
    {
        // 2.1 计时器按扫描 tick 增加。
        key_obj->press_tick_cnt++;

        // 2.2 只有在“启用长按 + 尚未上报”的情况下才判定长按。
        if ((s_drv_key_cfg.long_press_ticks > 0U) && (!key_obj->long_press_reported))
        {
            // 2.3 达到阈值后上报一次长按。
            if (key_obj->press_tick_cnt >= s_drv_key_cfg.long_press_ticks)
            {
                drv_key_set_pending_event(key_id, DRV_KEY_EVENT_LONG_PRESS);
                key_obj->long_press_reported = true;
            }
        }
    }
    else
    {
        // 3. 检测到松开后，先进入“第一次释放消抖态”。
        key_obj->debounce_cnt = 0U;
        next_state = DRV_KEY_ST_RELEASE_DEBOUNCE;
    }

    // 4. 返回下一状态。
    return next_state;
}

/**
 * @brief 处理第一次释放消抖态
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param key_obj 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static drv_key_state_e drv_key_handle_release_debounce(uint8_t key_id, drv_key_obj_t *key_obj)
{
    drv_key_state_e next_state = DRV_KEY_ST_RELEASE_DEBOUNCE; // 默认保持该状态

    // 1. 参数保护：对象为空时回空闲。
    if (key_obj == NULL)
    {
        return DRV_KEY_ST_IDLE;
    }

    // 2. 如果电平保持释放，则累计释放消抖计数。
    if (!drv_key_is_pressed(key_id))
    {
        key_obj->debounce_cnt++;

        // 3. 达到释放消抖阈值后，开始根据规则输出事件。
        if (key_obj->debounce_cnt >= s_drv_key_cfg.debounce_ticks)
        {
            // 3.1 如果本次已经上报过长按，则本次按键流程结束，直接回空闲。
            if (key_obj->long_press_reported)
            {
                drv_key_reset_key_runtime(key_obj);
                next_state = DRV_KEY_ST_IDLE;
            }
            else
            {
                // 3.2 未触发长按：根据是否启用双击决定“立即单击”还是“等待第二击”。
                if (s_drv_key_cfg.double_click_ticks == 0U)
                {
                    // 3.2.1 未启用双击：直接上报单击并回空闲。
                    drv_key_set_pending_event(key_id, DRV_KEY_EVENT_CLICK);
                    drv_key_reset_key_runtime(key_obj);
                    next_state = DRV_KEY_ST_IDLE;
                }
                else
                {
                    // 3.2.2 启用双击：进入等待第二击窗口，先不立即上报单击。
                    key_obj->wait_second_tick_cnt = 0U;
                    next_state = DRV_KEY_ST_WAIT_SECOND_CLICK;
                }
            }
        }
    }
    else
    {
        // 4. 释放消抖期间如果又按下，说明抖动或回弹，回到按下保持态。
        next_state = DRV_KEY_ST_PRESSED;
    }

    // 5. 返回下一状态。
    return next_state;
}

/**
 * @brief 处理等待第二击窗口态
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param key_obj 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static drv_key_state_e drv_key_handle_wait_second_click(uint8_t key_id, drv_key_obj_t *key_obj)
{
    drv_key_state_e next_state = DRV_KEY_ST_WAIT_SECOND_CLICK; // 默认保持该状态

    // 1. 参数保护：对象为空时回空闲。
    if (key_obj == NULL)
    {
        return DRV_KEY_ST_IDLE;
    }

    // 2. 如果窗口内检测到再次按下，进入第二次按下消抖态。
    if (drv_key_is_pressed(key_id))
    {
        key_obj->debounce_cnt = 0U;
        next_state = DRV_KEY_ST_SECOND_PRESS_DEBOUNCE;
    }
    else
    {
        // 3. 如果还没按下第二次，则累计等待窗口计时。
        key_obj->wait_second_tick_cnt++;

        // 4. 超过双击窗口仍无第二击，则确认单击事件。
        if (key_obj->wait_second_tick_cnt >= s_drv_key_cfg.double_click_ticks)
        {
            drv_key_set_pending_event(key_id, DRV_KEY_EVENT_CLICK);
            drv_key_reset_key_runtime(key_obj);
            next_state = DRV_KEY_ST_IDLE;
        }
    }

    // 5. 返回下一状态。
    return next_state;
}

/**
 * @brief 处理第二次按下消抖态
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param key_obj 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static drv_key_state_e drv_key_handle_second_press_debounce(uint8_t key_id, drv_key_obj_t *key_obj)
{
    drv_key_state_e next_state = DRV_KEY_ST_SECOND_PRESS_DEBOUNCE; // 默认保持该状态

    // 1. 参数保护：对象为空时回空闲。
    if (key_obj == NULL)
    {
        return DRV_KEY_ST_IDLE;
    }

    // 2. 如果第二次按下持续成立，则累计消抖计数。
    if (drv_key_is_pressed(key_id))
    {
        key_obj->debounce_cnt++;

        // 3. 达到阈值，确认第二次按下成立，进入第二次按下保持态。
        if (key_obj->debounce_cnt >= s_drv_key_cfg.debounce_ticks)
        {
            next_state = DRV_KEY_ST_SECOND_PRESSED;
        }
    }
    else
    {
        // 4. 第二次按下消抖期间松开，回到“等待第二击窗口态”。
        next_state = DRV_KEY_ST_WAIT_SECOND_CLICK;
    }

    // 5. 返回下一状态。
    return next_state;
}

/**
 * @brief 处理第二次按下保持态
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param key_obj 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static drv_key_state_e drv_key_handle_second_pressed(uint8_t key_id, drv_key_obj_t *key_obj)
{
    drv_key_state_e next_state = DRV_KEY_ST_SECOND_PRESSED; // 默认保持该状态

    // 1. 参数保护：对象为空时回空闲。
    if (key_obj == NULL)
    {
        return DRV_KEY_ST_IDLE;
    }

    // 2. 只要检测到第二次松开，就进入第二次释放消抖态。
    if (!drv_key_is_pressed(key_id))
    {
        key_obj->debounce_cnt = 0U;
        next_state = DRV_KEY_ST_SECOND_RELEASE_DEBOUNCE;
    }

    // 3. 返回下一状态。
    return next_state;
}

/**
 * @brief 处理第二次释放消抖态
 */
/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param key_id 函数输入参数，语义由调用场景决定。
 * @param key_obj 函数输入参数，语义由调用场景决定。
 * @return 返回函数执行结果。
 */
static drv_key_state_e drv_key_handle_second_release_debounce(uint8_t key_id, drv_key_obj_t *key_obj)
{
    drv_key_state_e next_state = DRV_KEY_ST_SECOND_RELEASE_DEBOUNCE; // 默认保持该状态

    // 1. 参数保护：对象为空时回空闲。
    if (key_obj == NULL)
    {
        return DRV_KEY_ST_IDLE;
    }

    // 2. 如果第二次释放持续成立，则累计释放消抖计数。
    if (!drv_key_is_pressed(key_id))
    {
        key_obj->debounce_cnt++;

        // 3. 达到阈值后，确认双击成立并上报双击事件。
        if (key_obj->debounce_cnt >= s_drv_key_cfg.debounce_ticks)
        {
            drv_key_set_pending_event(key_id, DRV_KEY_EVENT_DOUBLE_CLICK);
            drv_key_reset_key_runtime(key_obj);
            next_state = DRV_KEY_ST_IDLE;
        }
    }
    else
    {
        // 4. 第二次释放消抖期间又按下，回到第二次按下保持态。
        next_state = DRV_KEY_ST_SECOND_PRESSED;
    }

    // 5. 返回下一状态。
    return next_state;
}

/* ============================ 对外接口实现 ============================ */

/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param cfg 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool drv_key_init(const drv_key_cfg_t *cfg)
{
    uint16_t debounce_ticks; // 实际使用的消抖计数
    uint16_t long_press_ticks; // 实际使用的长按阈值
    uint16_t double_click_ticks; // 实际使用的双击窗口

    // 1. 参数校验：配置指针不能为空。
    if (cfg == NULL)
    {
        return false;
    }

    // 2. 参数校验：按键数量必须在有效范围内。
    if ((cfg->key_num == 0U) || (cfg->key_num > DRV_KEY_MAX_NUM))
    {
        return false;
    }

    // 3. 参数校验：按键读取回调不能为空。
    if (cfg->read_cb == NULL)
    {
        return false;
    }

    // 4. 处理消抖参数：传 0 则使用默认值。
    debounce_ticks = cfg->debounce_ticks;
    if (debounce_ticks == 0U)
    {
        debounce_ticks = DRV_KEY_DEFAULT_DEBOUNCE_TICKS;
    }

    // 5. 处理长按参数：传 0 表示关闭长按；非 0 直接使用传入值。
    long_press_ticks = cfg->long_press_ticks;

    // 6. 处理双击参数：传 0 表示关闭双击；非 0 直接使用传入值。
    double_click_ticks = cfg->double_click_ticks;

    // 7. 清空并写入新配置，避免旧配置残留。
    memset(&s_drv_key_cfg, 0, sizeof(s_drv_key_cfg));
    s_drv_key_cfg.key_num = cfg->key_num;
    s_drv_key_cfg.debounce_ticks = debounce_ticks;
    s_drv_key_cfg.long_press_ticks = long_press_ticks;
    s_drv_key_cfg.double_click_ticks = double_click_ticks;
    s_drv_key_cfg.read_cb = cfg->read_cb;
    s_drv_key_cfg.user_arg = cfg->user_arg;

    // 8. 清空所有按键对象状态。
    memset(s_drv_key_objs, 0, sizeof(s_drv_key_objs));

    // 9. 显式把每个按键状态机状态设为空闲态。
    for (uint8_t i = 0U; i < s_drv_key_cfg.key_num; i++) // 循环计数器
    {
        s_drv_key_objs[i].state = DRV_KEY_ST_IDLE;
    }

    // 10. 清空待上报事件缓存。
    s_drv_key_pending_evt.key_id = 0U;
    s_drv_key_pending_evt.type = DRV_KEY_EVENT_NONE;

    // 11. 标记初始化完成。
    s_drv_key_inited = true;

    // 12. 返回初始化成功。
    return true;
}

/**
 * @brief 执行驱动层硬件访问与基础控制。
 * @param out_event 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool drv_key_scan(drv_key_event_t *out_event)
{
    // 1. 参数校验：输出事件指针不能为空。
    if (out_event == NULL)
    {
        return false;
    }

    // 2. 每次扫描开始都先输出“无事件”。
    out_event->key_id = 0U;
    out_event->type = DRV_KEY_EVENT_NONE;

    // 3. 若驱动未初始化，直接返回失败。
    if (!s_drv_key_inited)
    {
        return false;
    }

    // 4. 逐个按键推进状态机。
    for (uint8_t i = 0U; i < s_drv_key_cfg.key_num; i++) // 循环计数器
    {
        drv_key_obj_t *key_obj = &s_drv_key_objs[i]; // 当前按键对象

        // 4.1 根据当前状态分发到对应处理函数。
        switch (key_obj->state)
        {
        case DRV_KEY_ST_IDLE:
            key_obj->state = drv_key_handle_idle(i, key_obj);
            break;

        case DRV_KEY_ST_PRESS_DEBOUNCE:
            key_obj->state = drv_key_handle_press_debounce(i, key_obj);
            break;

        case DRV_KEY_ST_PRESSED:
            key_obj->state = drv_key_handle_pressed(i, key_obj);
            break;

        case DRV_KEY_ST_RELEASE_DEBOUNCE:
            key_obj->state = drv_key_handle_release_debounce(i, key_obj);
            break;

        case DRV_KEY_ST_WAIT_SECOND_CLICK:
            key_obj->state = drv_key_handle_wait_second_click(i, key_obj);
            break;

        case DRV_KEY_ST_SECOND_PRESS_DEBOUNCE:
            key_obj->state = drv_key_handle_second_press_debounce(i, key_obj);
            break;

        case DRV_KEY_ST_SECOND_PRESSED:
            key_obj->state = drv_key_handle_second_pressed(i, key_obj);
            break;

        case DRV_KEY_ST_SECOND_RELEASE_DEBOUNCE:
            key_obj->state = drv_key_handle_second_release_debounce(i, key_obj);
            break;

        default:
            // 4.2 非法状态保护：复位该按键对象并回空闲。
            drv_key_reset_key_runtime(key_obj);
            key_obj->state = DRV_KEY_ST_IDLE;
            break;
        }
    }

    // 5. 把 pending 事件复制给上层。
    *out_event = s_drv_key_pending_evt;

    // 6. 清空 pending 槽，等待下次新事件写入。
    s_drv_key_pending_evt.key_id = 0U;
    s_drv_key_pending_evt.type = DRV_KEY_EVENT_NONE;

    // 7. 返回扫描成功。
    return true;
}

