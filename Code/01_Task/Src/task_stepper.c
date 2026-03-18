#include "task_stepper.h"
#include "task_init.h"
#include <string.h>

#if (TASK_STEPPER_ENABLE_CH1 == 0U) && (TASK_STEPPER_ENABLE_CH2 == 0U)
#error "At least one stepper channel must be enabled."
#endif

#define TASK_STEPPER_CHANNEL_COUNT ((uint8_t)(TASK_STEPPER_ENABLE_CH1 + TASK_STEPPER_ENABLE_CH2))

/**
 * @brief 单通道编译期配置结构。
 *
 * 这是任务层映射数据，不是协议层状态：
 * - logic_id：业务逻辑使用的电机标识
 * - driver_addr：协议发送时使用的目标地址
 * - huart：该通道实际使用的硬件串口
 */
typedef struct
{
    UART_HandleTypeDef *huart; // 该通道绑定的物理串口句柄。一个通道只对应一个 UART。
    uint8_t logic_id;           // 逻辑电机ID（业务层ID）。用于把 K230 帧中的 motor_id 路由到该通道。
    uint8_t driver_addr;        // 协议驱动地址（从站地址）。用于生成 stepper 协议帧的目标地址字段。
    uint16_t max_speed_rpm;     // 通道最大速度限制（RPM）。任务层在发送前会先按此值裁剪速度。
    bool positive_err_is_cw;    // 误差符号映射策略：true=正误差->CW，false=正误差->CCW。
} task_stepper_channel_cfg_t;

/**
 * @brief 单通道运行对象。
 *
 * 包含两部分：
 * 1. mod_ctx：协议层上下文
 * 2. state：任务层可观测状态
 */
typedef struct
{
    mod_stepper_ctx_t mod_ctx;   // 协议层上下文。负责该通道的 UART 绑定、组帧发送和发送状态机维护。
    task_stepper_state_t state;  // 任务层观测状态。用于记录绑定结果、最近误差、最近命令与发送统计。
} task_stepper_channel_t;

/* ------------------------- 通道静态配置表 ------------------------- */
static const task_stepper_channel_cfg_t s_channel_cfg[TASK_STEPPER_CHANNEL_COUNT] =
{
#if (TASK_STEPPER_ENABLE_CH1 == 1U)
    {
        .huart = TASK_STEPPER_CH1_HUART,
        .logic_id = TASK_STEPPER_CH1_LOGIC_ID,
        .driver_addr = TASK_STEPPER_CH1_DRIVER_ADDR,
        .max_speed_rpm = TASK_STEPPER_CH1_MAX_SPEED_RPM,
        .positive_err_is_cw = TASK_STEPPER_CH1_POS_ERR_IS_CW
    },
#endif
#if (TASK_STEPPER_ENABLE_CH2 == 1U)
    {
        .huart = TASK_STEPPER_CH2_HUART,
        .logic_id = TASK_STEPPER_CH2_LOGIC_ID,
        .driver_addr = TASK_STEPPER_CH2_DRIVER_ADDR,
        .max_speed_rpm = TASK_STEPPER_CH2_MAX_SPEED_RPM,
        .positive_err_is_cw = TASK_STEPPER_CH2_POS_ERR_IS_CW
    },
#endif
};

/* ------------------------- 通道运行存储 ------------------------- */
static task_stepper_channel_t s_channels[TASK_STEPPER_CHANNEL_COUNT];

/* ------------------------- 准备完成标志 ------------------------- */
static bool s_channels_ready = false;

/**
 * @brief 根据逻辑ID查找通道对象。
 * @param logic_id 逻辑电机ID。
 * @return 匹配通道指针；未找到返回 NULL。
 */
static task_stepper_channel_t *_find_channel(uint8_t logic_id)
{
    for (uint16_t i = 0U; i < TASK_STEPPER_CHANNEL_COUNT; i++)
    {
        if (s_channels[i].state.configured && (s_channels[i].state.logic_id == logic_id))
        {
            return &s_channels[i];
        }
    }

    return NULL;
}

/**
 * @brief 按通道上限约束速度。
 *
 * 规则：
 * 1. 若通道未配置有效上限，则退化到协议全局上限。
 * 2. 请求速度超过上限时裁剪到上限。
 */
static uint16_t _limit_speed_by_channel(const task_stepper_channel_t *ch, uint16_t req_speed_rpm)
{
    uint16_t limit_speed;

    if (ch == NULL)
    {
        return req_speed_rpm;
    }

    limit_speed = ch->state.max_speed_rpm;
    if ((limit_speed == 0U) || (limit_speed > MOD_STEPPER_VEL_MAX_RPM))
    {
        limit_speed = MOD_STEPPER_VEL_MAX_RPM;
    }

    if (req_speed_rpm > limit_speed)
    {
        return limit_speed;
    }

    return req_speed_rpm;
}

/**
 * @brief 将 int16 误差安全转换为无符号绝对值。
 */
static uint32_t _abs_i16_to_u32(int16_t value)
{
    if (value >= 0)
    {
        return (uint32_t)value;
    }
    return (uint32_t)(-(int32_t)value);
}

/**
 * @brief 将 K230 误差转换为脉冲命令值。
 *
 * 转换流程：
 * 1. 死区过滤：误差在死区内直接返回 0（本周期不修正）
 * 2. 线性放大：pulse = abs(err) * 比例系数
 * 3. 非零最小值约束：防止微小误差被量化后丢失
 * 4. 最大值约束：限制单周期修正量
 */
static uint32_t _error_to_pulse(int16_t err)
{
    uint32_t abs_err;
    uint32_t pulse;

    if ((_abs_i16_to_u32(err)) <= (uint32_t)TASK_STEPPER_ERR_DEADBAND)
    {
        return 0U;
    }

    abs_err = _abs_i16_to_u32(err);
    pulse = abs_err * TASK_STEPPER_PULSE_PER_ERR;

    if ((pulse > 0U) && (pulse < TASK_STEPPER_PULSE_MIN))
    {
        pulse = TASK_STEPPER_PULSE_MIN;
    }
    if (pulse > TASK_STEPPER_PULSE_MAX)
    {
        pulse = TASK_STEPPER_PULSE_MAX;
    }

    return pulse;
}

/**
 * @brief 根据误差符号与通道方向策略得到最终方向。
 *
 * 说明：
 * - positive_err_is_cw=true：正误差映射为 CW
 * - positive_err_is_cw=false：正误差映射为 CCW
 */
static mod_stepper_dir_e _error_to_dir(const task_stepper_channel_t *ch, int16_t err)
{
    bool positive = (err >= 0);

    if (ch == NULL)
    {
        return MOD_STEPPER_DIR_CW;
    }

    if (ch->state.positive_err_is_cw)
    {
        return positive ? MOD_STEPPER_DIR_CW : MOD_STEPPER_DIR_CCW;
    }

    return positive ? MOD_STEPPER_DIR_CCW : MOD_STEPPER_DIR_CW;
}

/**
 * @brief 对单个通道执行一次“误差 -> 位置修正命令”。
 *
 * 逻辑步骤：
 * 1. 更新观测状态（last_err/last_pulse_cmd）
 * 2. 若脉冲为0，直接跳过发送
 * 3. 计算方向与速度（含限幅）
 * 4. 发送位置模式命令
 * 5. 成功/失败分别累计计数
 */
static void _apply_error_correction(task_stepper_channel_t *ch, int16_t err)
{
    uint32_t pulse;
    mod_stepper_dir_e dir;
    uint16_t speed;

    if ((ch == NULL) || !ch->state.bound)
    {
        return;
    }

    ch->state.last_err = err;
    pulse = _error_to_pulse(err);
    ch->state.last_pulse_cmd = pulse;

    if (pulse == 0U)
    {
        return;
    }

    dir = _error_to_dir(ch, err);
    speed = _limit_speed_by_channel(ch, ch->state.max_speed_rpm);
    if (speed == 0U)
    {
        ch->state.tx_drop_count++;
        return;
    }

    if (mod_stepper_position(&ch->mod_ctx,
                             dir,
                             speed,
                             TASK_STEPPER_POS_ACC,
                             pulse,
                             TASK_STEPPER_POS_ABSOLUTE,
                             TASK_STEPPER_POS_SYNC_FLAG))
    {
        ch->state.last_dir = dir;
        ch->state.tx_cmd_count++;
    }
    else
    {
        ch->state.tx_drop_count++;
    }
}

/**
 * @brief 推进所有通道的发送状态机。
 *
 * 说明：
 * - TX-only 设计下，发送完成依赖轮询 process。
 * - 建议每个周期都调用一次，避免 tx_active 长时间不刷新。
 */
static void _process_all_channels(void)
{
    for (uint16_t i = 0U; i < TASK_STEPPER_CHANNEL_COUNT; i++)
    {
        if (s_channels[i].state.bound)
        {
            mod_stepper_process(&s_channels[i].mod_ctx);
        }
    }
}

/**
 * @brief 消费 K230 最新一帧并按逻辑ID分发到两个电机。
 *
 * 当前K230帧结构包含 motor1/motor2 两组 ID+误差：
 * - 按 frame.motor1_id 查找通道并修正 frame.err1
 * - 按 frame.motor2_id 查找通道并修正 frame.err2
 */
static void _consume_k230_latest(void)
{
    mod_k230_ctx_t *k230_ctx;
    mod_k230_frame_data_t frame;
    task_stepper_channel_t *ch;

    k230_ctx = mod_k230_get_default_ctx();
    if (!mod_k230_is_bound(k230_ctx))
    {
        return;
    }

    if (!mod_k230_get_latest_frame(k230_ctx, &frame))
    {
        return;
    }

    ch = _find_channel(frame.motor1_id);
    _apply_error_correction(ch, frame.err1);

    ch = _find_channel(frame.motor2_id);
    _apply_error_correction(ch, frame.err2);
}

bool task_stepper_prepare_channels(void)
{
    bool all_ok = true;
    mod_stepper_bind_t bind_cfg;

    memset(s_channels, 0, sizeof(s_channels));

    /**
     * 该流程设计为由 InitTask 调用：
     * 1. 在系统初始化阶段统一完成 UART 绑定与协议上下文初始化
     * 2. StepperTask 仅负责运行时控制，不再承担绑定职责
     */
    for (uint16_t i = 0U; i < TASK_STEPPER_CHANNEL_COUNT; i++)
    {
        memset(&bind_cfg, 0, sizeof(bind_cfg));
        bind_cfg.huart = s_channel_cfg[i].huart;
        bind_cfg.tx_mutex = NULL;
        bind_cfg.driver_addr = s_channel_cfg[i].driver_addr;

        s_channels[i].state.configured = true;
        s_channels[i].state.logic_id = s_channel_cfg[i].logic_id;
        s_channels[i].state.driver_addr = s_channel_cfg[i].driver_addr;
        s_channels[i].state.max_speed_rpm = s_channel_cfg[i].max_speed_rpm;
        s_channels[i].state.positive_err_is_cw = s_channel_cfg[i].positive_err_is_cw;
        s_channels[i].state.last_dir = MOD_STEPPER_DIR_CW;

        if (!mod_stepper_ctx_init(&s_channels[i].mod_ctx, &bind_cfg))
        {
            s_channels[i].state.bound = false;
            all_ok = false;
            continue;
        }

        s_channels[i].state.bound = true;
        mod_stepper_process(&s_channels[i].mod_ctx);

        if (TASK_STEPPER_ENABLE_AT_BIND)
        {
            if (mod_stepper_enable(&s_channels[i].mod_ctx, true, false))
            {
                s_channels[i].state.enabled = true;
                s_channels[i].state.tx_cmd_count++;
            }
            else
            {
                s_channels[i].state.enabled = false;
                s_channels[i].state.tx_drop_count++;
                all_ok = false;
            }
        }
    }

    s_channels_ready = true;
    return all_ok;
}

bool task_stepper_is_ready(void)
{
    return s_channels_ready;
}

bool task_stepper_send_velocity(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag)
{
    task_stepper_channel_t *ch;
    uint16_t limited_speed;

    ch = _find_channel(logic_id);
    if ((ch == NULL) || !ch->state.bound)
    {
        return false;
    }

    limited_speed = _limit_speed_by_channel(ch, vel_rpm);
    if (limited_speed == 0U)
    {
        ch->state.tx_drop_count++;
        return false;
    }

    if (mod_stepper_velocity(&ch->mod_ctx, dir, limited_speed, acc, sync_flag))
    {
        ch->state.tx_cmd_count++;
        return true;
    }

    ch->state.tx_drop_count++;
    return false;
}

bool task_stepper_send_position(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag)
{
    task_stepper_channel_t *ch;
    uint16_t limited_speed;

    ch = _find_channel(logic_id);
    if ((ch == NULL) || !ch->state.bound)
    {
        return false;
    }

    limited_speed = _limit_speed_by_channel(ch, vel_rpm);
    if (limited_speed == 0U)
    {
        ch->state.tx_drop_count++;
        return false;
    }

    if (mod_stepper_position(&ch->mod_ctx, dir, limited_speed, acc, pulse, absolute_mode, sync_flag))
    {
        ch->state.tx_cmd_count++;
        ch->state.last_dir = dir;
        ch->state.last_pulse_cmd = pulse;
        return true;
    }

    ch->state.tx_drop_count++;
    return false;
}

void StartStepperTask(void *argument)
{
    (void)argument;

    /**
     * 全局启动门控：
     * 等待 InitTask 完成全部绑定与基础初始化后再进入控制循环。
     */
    task_wait_init_done();

#if (TASK_STEPPER_STARTUP_ENABLE == 0U)
    (void)osThreadSuspend(osThreadGetId());
    for (;;)
    {
        osDelay(osWaitForever);
    }
#endif

    /**
     * 兜底逻辑：
     * 若因某些原因 InitTask 未调用 prepare，这里补做一次，
     * 保证任务仍可进入可用状态。
     */
    if (!task_stepper_is_ready())
    {
        (void)task_stepper_prepare_channels();
    }

    for (;;)
    {
        /**
         * 每周期执行顺序：
         * 1. 刷新所有通道发送状态
         * 2. 读取 K230 最新帧并执行双电机修正
         */
        _process_all_channels();
        _consume_k230_latest();

        osDelay(TASK_STEPPER_PERIOD_MS);
    }
}

const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id)
{
    task_stepper_channel_t *ch = _find_channel(logic_id);

    if (ch == NULL)
    {
        return NULL;
    }

    return &ch->state;
}
