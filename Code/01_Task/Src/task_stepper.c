#include "task_stepper.h"
#include "task_init.h"
#include "mod_vofa.h"
#include <string.h>

/**
 * @brief Stepper任务就绪标志
 *
 * 由task_stepper_prepare_channels()置位。
 * StartStepperTask中会做兜底检查，未准备时自动补一次prepare。
 */
static bool s_stepper_ready = false;

/**
 * @brief Stepper任务全局状态缓存
 *
 * 说明：
 * 1. 该缓存由Stepper任务主循环单线程更新；
 * 2. 其他任务可通过task_stepper_get_state()只读访问；
 * 3. 当前版本不维护多通道上下文，仅维护一份观测快照。
 */
static task_stepper_state_t s_stepper_state;

/**
 * @brief 从K230模块读取“最新有效帧”，并刷新本地缓存
 *
 * 关键行为：
 * 1. 先检查K230是否已绑定，未绑定则直接返回；
 * 2. 调用mod_k230_get_latest_frame()获取缓冲区中最新一帧；
 * 3. 若本周期无新帧，不覆盖旧缓存，保持上一帧观测值；
 * 4. 成功更新时递增frame_update_count。
 *
 * 备注：
 * mod_k230_get_latest_frame()内部会消费其接收缓冲区，并返回“当前可解析到的最后一帧”。
 */
static void _update_latest_k230_frame(void)
{
    mod_k230_ctx_t *k230_ctx;
    mod_k230_frame_data_t frame;

    k230_ctx = mod_k230_get_default_ctx();
    s_stepper_state.k230_bound = mod_k230_is_bound(k230_ctx);
    if (!s_stepper_state.k230_bound)
    {
        return;
    }

    if (!mod_k230_get_latest_frame(k230_ctx, &frame))
    {
        return;
    }

    s_stepper_state.motor1_id = frame.motor1_id;
    s_stepper_state.err1 = frame.err1;
    s_stepper_state.motor2_id = frame.motor2_id;
    s_stepper_state.err2 = frame.err2;
    s_stepper_state.frame_update_count++;
}

/**
 * @brief 将本地缓存的4个观测量通过VOFA发送
 *
 * 发送内容顺序固定为：
 * 1. motor1_id
 * 2. err1
 * 3. motor2_id
 * 4. err2
 *
 * 统计规则：
 * 1. VOFA未绑定时，计入vofa_tx_drop_count；
 * 2. mod_vofa_send_float_ctx()返回true时，计入vofa_tx_ok_count；
 * 3. 返回false时，计入vofa_tx_drop_count。
 */
static void _send_cached_frame_to_vofa(void)
{
    mod_vofa_ctx_t *vofa_ctx;
    float vofa_payload[4];

    vofa_ctx = mod_vofa_get_default_ctx();
    s_stepper_state.vofa_bound = mod_vofa_is_bound(vofa_ctx);
    if (!s_stepper_state.vofa_bound)
    {
        s_stepper_state.vofa_tx_drop_count++;
        return;
    }

    vofa_payload[0] = (float)s_stepper_state.motor1_id;
    vofa_payload[1] = (float)s_stepper_state.err1;
    vofa_payload[2] = (float)s_stepper_state.motor2_id;
    vofa_payload[3] = (float)s_stepper_state.err2;

    if (mod_vofa_send_float_ctx(vofa_ctx, TASK_STEPPER_VOFA_TAG, vofa_payload, 4U))
    {
        s_stepper_state.vofa_tx_ok_count++;
    }
    else
    {
        s_stepper_state.vofa_tx_drop_count++;
    }
}

bool task_stepper_prepare_channels(void)
{
    memset(&s_stepper_state, 0, sizeof(s_stepper_state));
    s_stepper_state.configured = true;
    s_stepper_ready = true;
    return true;
}

bool task_stepper_is_ready(void)
{
    return s_stepper_ready;
}

bool task_stepper_send_velocity(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag)
{
    (void)logic_id;
    (void)dir;
    (void)vel_rpm;
    (void)acc;
    (void)sync_flag;
    return false;
}

bool task_stepper_send_position(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag)
{
    (void)logic_id;
    (void)dir;
    (void)vel_rpm;
    (void)acc;
    (void)pulse;
    (void)absolute_mode;
    (void)sync_flag;
    return false;
}

/**
 * @brief Stepper任务主函数
 *
 * 执行流程：
 * 1. 等待全局初始化完成（task_wait_init_done）；
 * 2. 若配置为禁用启动，则主动挂起当前任务；
 * 3. 若未prepare，则执行一次兜底prepare；
 * 4. 进入20ms循环：更新K230最新帧 -> 发送VOFA观测数据。
 */
void StartStepperTask(void *argument)
{
    (void)argument;

    task_wait_init_done();

#if (TASK_STEPPER_STARTUP_ENABLE == 0U)
    (void)osThreadSuspend(osThreadGetId());
    for (;;)
    {
        osDelay(osWaitForever);
    }
#endif

    if (!task_stepper_is_ready())
    {
        (void)task_stepper_prepare_channels();
    }

    for (;;)
    {
        _update_latest_k230_frame();
        _send_cached_frame_to_vofa();
        osDelay(TASK_STEPPER_PERIOD_MS);
    }
}

const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id)
{
    (void)logic_id;

    if (!s_stepper_ready)
    {
        return NULL;
    }

    return &s_stepper_state;
}
