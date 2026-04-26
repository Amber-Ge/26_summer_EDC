/**
 * @file    task_debug.c
 * @author  Amber Ge
 * @brief   Debug 任务：执行当前测试模式并向 VOFA 发布观测量。
 */

#include "task_debug.h"

#include "cmsis_os.h"
#include "stm32f4xx_hal.h"
#include "main.h"
#include "task_init.h"

#include "mod_led.h"
#include "mod_k230.h"
#include "mod_vision.h"
#include "mod_vofa.h"

#define TASK_DEBUG_DEFAULT_MODE           (TASK_DEBUG_MODE_TEST_K230_VOFA)
#define TASK_DEBUG_LOOP_DELAY_MS          (50U)
#define TASK_DEBUG_K230_STALE_TIMEOUT_MS  (500U)
#define TASK_DEBUG_KEY_DEBOUNCE_MS        (30U)

static volatile task_debug_mode_t s_debug_mode = TASK_DEBUG_DEFAULT_MODE;
static uint32_t s_debug_vofa_send_ok = 0U;
static uint32_t s_debug_vofa_send_fail = 0U;

static GPIO_PinState task_debug_read_user_key(void)
{
    return HAL_GPIO_ReadPin(Key_User_GPIO_Port, Key_User_Pin);
}

/*
 * 测试模式：TASK_DEBUG_MODE_TEST_VOFA_COUNTER
 *
 * 这是最基础的链路自检模式，只验证两件事：
 * 1. Debug 任务主循环是否在按周期稳定执行。
 * 2. VOFA 整型数据发送接口是否工作正常。
 *
 * 发送主题：test_vofa_counter
 * 发送字段：
 * payload[0] = test_count
 *   Debug 任务进入本模式后的累计执行次数。该值应随任务循环单调递增，
 *   若 VOFA 曲线/数据显示停住，通常意味着任务没有继续运行，或发送链路中断。
 *
 * payload[1] = s_debug_vofa_send_ok
 *   历史 VOFA 发送成功次数。注意这里发送的是“本次发送前”的成功计数，
 *   因为计数更新发生在 mod_vofa_send_int() 返回之后。
 *
 * payload[2] = s_debug_vofa_send_fail
 *   历史 VOFA 发送失败次数。若该值持续增长，说明串口、缓存、协议或上位机接收链路存在问题。
 *
 * 适用场景：
 * - 新接入 VOFA 时先做冒烟测试。
 * - 排查“任务有没有跑起来”。
 * - 排查“VOFA 能不能稳定收到数据”。
 */
static void task_debug_run_test_vofa_counter(void)
{
    static uint32_t test_count = 0U;
    int32_t payload[3];
    bool send_ok;

    test_count++;
    payload[0] = (int32_t)test_count;
    payload[1] = (int32_t)s_debug_vofa_send_ok;
    payload[2] = (int32_t)s_debug_vofa_send_fail;
    send_ok = mod_vofa_send_int("test_vofa_counter", payload, 3U);
    if (send_ok)
    {
        s_debug_vofa_send_ok++;
    }
    else
    {
        s_debug_vofa_send_fail++;
    }
}

bool task_debug_set_mode(task_debug_mode_t mode)
{
    if (mode >= TASK_DEBUG_MODE_MAX)
    {
        return false;
    }

    s_debug_mode = mode;
    return true;
}

task_debug_mode_t task_debug_get_mode(void)
{
    return s_debug_mode;
}

/*
 * 测试模式：TASK_DEBUG_MODE_TEST_K230_VOFA
 *
 * 该模式用于同时观察“上层视觉结果”和“K230 原始协议帧”两类信息，
 * 适合排查从 K230 到视觉解析模块，再到 VOFA 输出这一整条链路。
 *
 * 第一组发送主题：test_k230_vofa
 * 发送字段：
 * payload[0] = has_data
 *   1 表示成功获取到最新视觉结果，0 表示当前没有可用视觉数据。
 *
 * payload[1] = data_fresh
 *   1 表示视觉数据在超时时间内，认为是新鲜数据；
 *   0 表示数据已超时，虽然可能还能读到上一次结果，但不建议继续用于控制。
 *
 * payload[2] = vision_data.source
 *   数据源标识，用于区分当前视觉结果来自哪个来源/通道。
 *
 * payload[3] = vision_data.update_seq
 *   视觉数据更新序号。正常情况下应持续变化，可用于判断数据是否真的在刷新。
 *
 * payload[4] = vision_data.x_target_id
 *   X 方向当前跟踪目标的编号。
 *
 * payload[5] = vision_data.x_error
 *   X 方向误差量。通常用于观察目标偏差是否符合预期。
 *
 * payload[6] = vision_data.y_target_id
 *   Y 方向当前跟踪目标的编号。
 *
 * payload[7] = vision_data.y_error
 *   Y 方向误差量。
 *
 * payload[8] = data_age_ms
 *   当前时刻距离 vision_data.update_tick 的时间差，单位 ms。
 *   数值越大，说明这份数据越旧。
 *
 * payload[9] = s_debug_vofa_send_ok
 * payload[10] = s_debug_vofa_send_fail
 *   累计发送成功/失败次数，用于同时观察调试输出链路健康状态。
 *
 * 特殊情况：
 * - 如果 mod_vision_get_latest_data() 失败，说明当前没有解析后的视觉结果可读。
 *   这时函数仍会发送一帧 test_k230_vofa，但除成功/失败计数外其余字段保持 0，
 *   便于在 VOFA 上直接看出“没有视觉数据”而不是“调试任务没运行”。
 *
 * 第二组发送主题：test_k230_raw
 * 发送内容：
 * - 将最近一次收到的 K230 原始协议帧逐字节展开为 int32_t 数组后发送。
 * - raw_payload[i] 对应原始帧的第 i 个字节。
 *
 * 适用场景：
 * - 上位视觉识别结果异常时，判断问题出在原始串口数据、协议解析，还是上层视觉整合。
 * - 对照协议文档检查 K230 发来的字节流内容。
 * - 观察视觉数据是否超时、序号是否连续刷新。
 */
static void task_debug_run_test_k230_vofa(void)
{
    mod_k230_ctx_t *k230_ctx = mod_k230_get_default_ctx();
    mod_vision_ctx_t *vision_ctx = mod_vision_get_default_ctx();
    mod_vision_data_t vision_data;
    uint8_t raw_frame[MOD_K230_PROTO_FRAME_SIZE] = {0};
    int32_t payload[11] = {0};
    int32_t raw_payload[MOD_K230_PROTO_FRAME_SIZE] = {0};
    bool send_ok;
    bool raw_send_ok;

    if (!mod_vision_get_latest_data(vision_ctx, &vision_data))
    {
        payload[9] = (int32_t)s_debug_vofa_send_ok;
        payload[10] = (int32_t)s_debug_vofa_send_fail;
        send_ok = mod_vofa_send_int("test_k230_vofa", payload, 11U);
        if (send_ok)
        {
            s_debug_vofa_send_ok++;
        }
        else
        {
            s_debug_vofa_send_fail++;
        }
        return;
    }

    payload[0] = 1;
    payload[1] = mod_vision_is_data_stale(vision_ctx, TASK_DEBUG_K230_STALE_TIMEOUT_MS) ? 0 : 1;
    payload[2] = (int32_t)vision_data.source;
    payload[3] = (int32_t)vision_data.update_seq;
    payload[4] = (int32_t)vision_data.x_target_id;
    payload[5] = (int32_t)vision_data.x_error;
    payload[6] = (int32_t)vision_data.y_target_id;
    payload[7] = (int32_t)vision_data.y_error;
    payload[8] = (int32_t)(HAL_GetTick() - vision_data.update_tick);
    payload[9] = (int32_t)s_debug_vofa_send_ok;
    payload[10] = (int32_t)s_debug_vofa_send_fail;
    send_ok = mod_vofa_send_int("test_k230_vofa", payload, 11U);
    if (send_ok)
    {
        s_debug_vofa_send_ok++;
    }
    else
    {
        s_debug_vofa_send_fail++;
    }

    if (mod_k230_get_latest_raw_frame(k230_ctx, raw_frame))
    {
        for (uint8_t i = 0U; i < MOD_K230_PROTO_FRAME_SIZE; i++)
        {
            raw_payload[i] = (int32_t)raw_frame[i];
        }

        raw_send_ok = mod_vofa_send_int("test_k230_raw", raw_payload, MOD_K230_PROTO_FRAME_SIZE);
        if (raw_send_ok)
        {
            s_debug_vofa_send_ok++;
        }
        else
        {
            s_debug_vofa_send_fail++;
        }
    }
}

/*
 * 测试模式：TASK_DEBUG_MODE_TEST_KEY_VOFA
 *
 * 该模式用于验证用户按键输入链路，包括：
 * 1. 原始 GPIO 电平是否正确。
 * 2. 消抖后稳定状态是否正确。
 * 3. 按下事件是否只统计一次，不会因机械抖动重复计数。
 *
 * 当前逻辑假设：
 * - 按键未按下时为 GPIO_PIN_SET。
 * - 按键按下时为 GPIO_PIN_RESET。
 * 即按键为低电平有效。
 *
 * 消抖流程：
 * 1. 先读取原始电平 key_state。
 * 2. 只要原始电平发生变化，就刷新 last_transition_tick。
 * 3. 若该状态持续保持超过 TASK_DEBUG_KEY_DEBOUNCE_MS，
 *    才把 stable_state 更新为新的稳定状态。
 * 4. 仅当稳定状态从 SET 变为 RESET 时，认为发生一次“有效按下”。
 *
 * 发送主题：test_key_vofa
 * 发送字段：
 * payload[0] = press_count
 *   已确认的有效按下次数。只有通过消抖并检测到按下沿后才会增加。
 *
 * payload[1] = raw_level
 *   原始按键电平。
 *   1 表示当前读取到高电平（通常是未按下），0 表示低电平（通常是按下）。
 *
 * payload[2] = stable_pressed
 *   消抖后的稳定按下状态。
 *   1 表示稳定按下，0 表示稳定未按下。
 *
 * payload[3] = edge_pulse
 *   有效按下沿脉冲。
 *   仅在本次循环刚检测到一次稳定按下沿时为 1，其余时间为 0。
 *   该字段适合直接在 VOFA 里观察“是否只触发一次”。
 *
 * payload[4] = s_debug_vofa_send_ok
 * payload[5] = s_debug_vofa_send_fail
 *   累计发送成功/失败次数，用于排除按键正常但调试输出链路异常的情况。
 *
 * 适用场景：
 * - 检查按键硬件电平极性是否接反。
 * - 调整或验证消抖时间是否合适。
 * - 检查一次按下是否被重复识别。
 */
static void task_debug_run_test_key_vofa(void)
{
    static uint32_t press_count = 0U;
    static GPIO_PinState last_raw_state = GPIO_PIN_SET;
    static GPIO_PinState stable_state = GPIO_PIN_SET;
    static GPIO_PinState last_stable_state = GPIO_PIN_SET;
    static uint32_t last_transition_tick = 0U;
    GPIO_PinState key_state;
    uint32_t now_tick;
    bool edge_pulse = false;
    int32_t payload[6];
    bool send_ok;

    key_state = task_debug_read_user_key();
    now_tick = HAL_GetTick();

    if (key_state != last_raw_state)
    {
        last_raw_state = key_state;
        last_transition_tick = now_tick;
    }

    if (((now_tick - last_transition_tick) >= TASK_DEBUG_KEY_DEBOUNCE_MS) &&
        (stable_state != last_raw_state))
    {
        stable_state = last_raw_state;
    }

    if ((last_stable_state == GPIO_PIN_SET) && (stable_state == GPIO_PIN_RESET))
    {
        press_count++;
        edge_pulse = true;
    }

    last_stable_state = stable_state;

    payload[0] = (int32_t)press_count;
    payload[1] = (key_state == GPIO_PIN_SET) ? 1 : 0;
    payload[2] = (stable_state == GPIO_PIN_RESET) ? 1 : 0;
    payload[3] = edge_pulse ? 1 : 0;
    payload[4] = (int32_t)s_debug_vofa_send_ok;
    payload[5] = (int32_t)s_debug_vofa_send_fail;
    send_ok = mod_vofa_send_int("test_key_vofa", payload, 6U);
    if (send_ok)
    {
        s_debug_vofa_send_ok++;
    }
    else
    {
        s_debug_vofa_send_fail++;
    }
}

void StartDebugTask(void *argument)
{
    mod_led_ctx_t *led_ctx = mod_led_get_default_ctx();
    (void)argument;

    task_wait_init_done();

    for (;;)
    {
        mod_led_toggle(led_ctx, LED_BROAD);

        /*
         * 根据当前调试模式切换不同的测试逻辑。
         * 新增测试模式时，应在 task_debug_mode_t 中补充枚举，
         * 并在这里挂接对应的执行函数。
         */
        switch (task_debug_get_mode())
        {
        case TASK_DEBUG_MODE_TEST_VOFA_COUNTER:
            task_debug_run_test_vofa_counter();
            break;
        case TASK_DEBUG_MODE_TEST_K230_VOFA:
            task_debug_run_test_k230_vofa();
            break;
        case TASK_DEBUG_MODE_TEST_KEY_VOFA:
            task_debug_run_test_key_vofa();
            break;
        case TASK_DEBUG_MODE_NONE:
        default:
            break;
        }

        osDelay(TASK_DEBUG_LOOP_DELAY_MS);
    }
}
