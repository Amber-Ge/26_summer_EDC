/**
 ******************************************************************************
 * @file    drv_key.c
 * @brief   KEY驱动实现（轮询扫描 + 消抖状态机）
 *
 * @note
 * - 本文件只 include 自己的头文件（按你的工程规范）
 * - HAL / GPIO_PIN_xxx / GPIOx 等定义均由 drv_key.h -> main.h 提供
 *
 ******************************************************************************
 */

#include "drv_key.h"

/** =========================== 可配置参数 =========================== */
/** 消抖确认次数：扫描周期10ms时，2次≈20ms */
#define DRV_KEY_DEBOUNCE_CNT          (2U)

/** 按键数量 */
#define DRV_KEY_NUM                  (3U)
/** ================================================================= */

/**
 * @brief 按键状态机状态定义
 */
typedef enum
{
    DRV_KEY_ST_IDLE = 0,          // 空闲态：等待按键按下
    DRV_KEY_ST_DEBOUNCE,          // 消抖态：连续检测低电平确认按下
    DRV_KEY_ST_PRESSED,           // 按下确认态：上报一次按下事件
    DRV_KEY_ST_WAIT_RELEASE,      // 等待释放态：防止长按重复触发
    DRV_KEY_ST_MAX                // 状态机状态数量上限
} DrvKeyState_e;

/**
 * @brief 单个按键对象
 */
typedef struct
{
    GPIO_TypeDef   *gpio_port;     // 按键对应的 GPIO 端口（例如 GPIOG）
    uint16_t        gpio_pin;      // 按键对应的 GPIO 引脚（例如 GPIO_PIN_2）

    DrvKeyState_e   state;         // 当前状态机状态
    uint8_t         debounce_cnt;  // 消抖计数，单位为扫描周期（10ms）

    DrvKeyEvent_e   press_event;   // 按下确认后上报给上层的事件类型
} DrvKeyObj_t;

/** -------------------- 状态机处理函数声明 -------------------- */
static DrvKeyState_e drv_key_handle_idle(DrvKeyObj_t *k);
static DrvKeyState_e drv_key_handle_debounce(DrvKeyObj_t *k);
static DrvKeyState_e drv_key_handle_pressed(DrvKeyObj_t *k);
static DrvKeyState_e drv_key_handle_wait_release(DrvKeyObj_t *k);

typedef DrvKeyState_e (*DrvKeyStateHandler_t)(DrvKeyObj_t *k); // 状态处理函数指针类型

static const DrvKeyStateHandler_t s_state_table[DRV_KEY_ST_MAX] = // 状态机处理表：状态 -> 处理函数
{
    [DRV_KEY_ST_IDLE]         = drv_key_handle_idle,
    [DRV_KEY_ST_DEBOUNCE]     = drv_key_handle_debounce,
    [DRV_KEY_ST_PRESSED]      = drv_key_handle_pressed,
    [DRV_KEY_ST_WAIT_RELEASE] = drv_key_handle_wait_release
};

/** -------------------- 硬件映射（只改这里就能换引脚） --------------------
 * KEY1 : PG2
 * KEY2 : PG3
 * KEY3 : PG4
 *
 * 重要：按键为接地按下(Active-Low)，CubeMX需配置 Pull-Up
 */
static DrvKeyObj_t s_keys[DRV_KEY_NUM] =
{
    { KEY_1_GPIO_Port, KEY_1_Pin, DRV_KEY_ST_IDLE, 0U, DRV_KEY_EVENT_1_PRESSED },
    { KEY_2_GPIO_Port, KEY_2_Pin, DRV_KEY_ST_IDLE, 0U, DRV_KEY_EVENT_2_PRESSED },
    { KEY_3_GPIO_Port, KEY_3_Pin, DRV_KEY_ST_IDLE, 0U, DRV_KEY_EVENT_3_PRESSED }
};

static DrvKeyEvent_e s_pending_event = DRV_KEY_EVENT_NONE; // 待上报事件：scan 返回后立即清零

// =========================== 对外接口 =========================== */

void drv_key_init(void)
{
    // 轮询方案无需额外初始化，若后续增加长按/连按计时，可以在这里清理内部变量
    s_pending_event = DRV_KEY_EVENT_NONE;

    for (uint32_t i = 0; i < DRV_KEY_NUM; i++)
    {
        s_keys[i].state = DRV_KEY_ST_IDLE;
        s_keys[i].debounce_cnt = 0U;
    }
}

DrvKeyEvent_e drv_key_scan(void)
{
    DrvKeyEvent_e evt; // 当前扫描周期要返回给上层的按键事件

    // 运行所有按键状态机
    for (uint32_t i = 0; i < DRV_KEY_NUM; i++)
    {
        DrvKeyObj_t *k = &s_keys[i];
        k->state = s_state_table[k->state](k);
    }

    // 取出事件并清除（保证一次只返回一次事件）
    evt = s_pending_event;
    s_pending_event = DRV_KEY_EVENT_NONE;

    return evt;
}

/* =========================== 状态机实现 =========================== */

static DrvKeyState_e drv_key_handle_idle(DrvKeyObj_t *k)
{
    DrvKeyState_e next_state; // 状态机下一状态

    next_state = DRV_KEY_ST_IDLE;

    // Active-Low：按下为 GPIO_PIN_RESET
    if (HAL_GPIO_ReadPin(k->gpio_port, k->gpio_pin) == GPIO_PIN_RESET)
    {
        k->debounce_cnt = 0U;
        next_state = DRV_KEY_ST_DEBOUNCE;
    }

    return next_state;
}

static DrvKeyState_e drv_key_handle_debounce(DrvKeyObj_t *k)
{
    DrvKeyState_e next_state; // 状态机下一状态

    next_state = DRV_KEY_ST_DEBOUNCE;

    // 仍然保持按下（低电平）才继续计数
    if (HAL_GPIO_ReadPin(k->gpio_port, k->gpio_pin) == GPIO_PIN_RESET)
    {
        k->debounce_cnt++;

        // 连续低电平 N 次（N * 10ms）确认按下
        if (k->debounce_cnt >= (uint8_t)DRV_KEY_DEBOUNCE_CNT)
        {
            next_state = DRV_KEY_ST_PRESSED;
        }
    }
    else
    {
        /* 抖动或松手：回到空闲 */
        next_state = DRV_KEY_ST_IDLE;
    }

    return next_state;
}

static DrvKeyState_e drv_key_handle_pressed(DrvKeyObj_t *k)
{
    DrvKeyState_e next_state; // 状态机下一状态

    // 确认按下时，上报一次事件
    s_pending_event = k->press_event;

    // 干完活进入等待松手，防止按住不放重复触发
    next_state = DRV_KEY_ST_WAIT_RELEASE;

    return next_state;
}

static DrvKeyState_e drv_key_handle_wait_release(DrvKeyObj_t *k)
{
    DrvKeyState_e next_state; // 状态机下一状态

    next_state = DRV_KEY_ST_WAIT_RELEASE;

    // 松手：回到高电平(GPIO_PIN_SET)
    if (HAL_GPIO_ReadPin(k->gpio_port, k->gpio_pin) == GPIO_PIN_SET)
        next_state = DRV_KEY_ST_IDLE;

    return next_state;
}
