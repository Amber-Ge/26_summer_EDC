/**
 * @file    task_dcc.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   DCC 控制任务实现。
 * @details
 * 1. DccTask 是底盘控制状态机唯一写入者，维护 OFF/PREPARE/ON/STOP 四态。
 * 2. 任务消费 KeyTask 释放的控制信号量，并按模式执行直线/循迹闭环。
 * 3. 任务通过 mod_motor/mod_sensor 与 PID 组件完成执行与反馈采样。
 */
#include "task_dcc.h"
#include "task_init.h"
#include "mod_vofa.h"
#include <stddef.h>

#define TASK_DCC_GRAY_STOP_COUNT (3U)      /* 直线模式：连续黑线触发阈值 */
#define TASK_DCC_STARTUP_DELAY_MS (3000U)  /* 上电后任务额外延时，避免启动瞬态 */
#define TASK_DCC_STRAIGHT_GUARD_MS (1000U) /* 直线模式启动后1秒传感器保护窗 */

/* 当前模式：0=IDLE, 1=STRAIGHT, 2=TRACK */
static volatile uint8_t s_dcc_mode = TASK_DCC_MODE_IDLE;
/* 当前运行状态：OFF / PREPARE / ON / STOP */
static volatile uint8_t s_dcc_run_state = TASK_DCC_RUN_OFF;

/**
 * @brief 对浮点数执行区间限幅。
 * @param value 待限幅值。
 * @param min 下限值。
 * @param max 上限值。
 * @return 限幅后的结果。
 */
static float clamp_float(float value, float min, float max)
{
    float result = value;

    /* 统一浮点限幅逻辑，避免每个控制分支重复写边界判断。 */
    if (result < min)
    {
        result = min;
    }
    if (result > max)
    {
        result = max;
    }

    return result;
}

/**
 * @brief 将浮点占空比换算为电机占空比整型命令。
 * @param duty_f 目标占空比（浮点）。
 * @return 已限幅并四舍五入后的占空比命令。
 */
static int16_t convert_to_duty_cmd(float duty_f)
{
    float duty_limited; /* 限幅后的浮点占空比 */

    /* 先按模块约束限幅，再执行四舍五入到 int16_t。 */
    duty_limited = clamp_float(duty_f, -(float)MOD_MOTOR_DUTY_MAX, (float)MOD_MOTOR_DUTY_MAX);
    if (duty_limited >= 0.0f)
    {
        duty_limited += 0.5f;
    }
    else
    {
        duty_limited -= 0.5f;
    }

    return (int16_t)duty_limited;
}

/**
 * @brief 立即停止左右电机输出。
 */
static void dcc_stop_motor_now(void)
{
    /* 所有状态迁移都以“先停机”作为安全动作。 */
    mod_motor_set_duty(mod_motor_get_default_ctx(), MOD_MOTOR_LEFT, 0);
    mod_motor_set_duty(mod_motor_get_default_ctx(), MOD_MOTOR_RIGHT, 0);
}

/**
 * @brief 重置位置环与左右速度环 PID 内部状态并恢复目标误差。
 * @param pos_pid 位置环 PID 对象。
 * @param left_speed_pid 左轮速度环 PID 对象。
 * @param right_speed_pid 右轮速度环 PID 对象。
 */
static void dcc_reset_pid(pid_pos_t *pos_pid, pid_inc_t *left_speed_pid, pid_inc_t *right_speed_pid)
{
    if ((pos_pid == NULL) || (left_speed_pid == NULL) || (right_speed_pid == NULL))
    {
        return;
    }

    /* 同时复位位置环与速度环，防止状态切换遗留积分项。 */
    PID_Pos_Reset(pos_pid);
    PID_Inc_Reset(left_speed_pid);
    PID_Inc_Reset(right_speed_pid);
    PID_Pos_SetTarget(pos_pid, MOTOR_TARGET_ERROR);
}

/**
 * @brief 将毫秒时间换算为 RTOS tick，向上取整且至少为 1 tick。
 * @param duration_ms 目标时长（毫秒）。
 * @param tick_freq RTOS tick 频率（Hz），传入 0 时按 1000Hz 兜底。
 * @return 换算后的 tick 数。
 */
static uint32_t dcc_ms_to_ticks(uint32_t duration_ms, uint32_t tick_freq)
{
    uint32_t ticks;
    if (tick_freq == 0U)
    {
        tick_freq = 1000U;
    }

    /* 向上取整，保证任意非零毫秒值至少对应 1 个调度 tick。 */
    ticks = (uint32_t)(((uint64_t)duration_ms * (uint64_t)tick_freq + 999ULL) / 1000ULL);
    if (ticks == 0U)
    {
        ticks = 1U;
    }

    return ticks;
}

/**
 * @brief 判断当前 tick 是否达到目标 tick（支持回绕）。
 * @param now_tick 当前 tick。
 * @param target_tick 目标 tick。
 * @return 达到返回 1，否则返回 0。
 */
static int dcc_time_reached(uint32_t now_tick, uint32_t target_tick)
{
    return ((int32_t)(now_tick - target_tick) >= 0);
}

/**
 * @brief 进入 OFF 安全态。
 * @details
 * 1. 写入全局运行状态为 OFF。
 * 2. 清空黑线触发计数，防止旧计数影响后续模式切换。
 * 3. 立即停机并复位 PID 内部状态。
 * @param pos_pid 位置环 PID 对象。
 * @param left_speed_pid 左轮速度环 PID 对象。
 * @param right_speed_pid 右轮速度环 PID 对象。
 * @param gray_trigger_streak 连续黑线计数器地址，可为 NULL。
 */
static void dcc_enter_off(pid_pos_t *pos_pid,
                          pid_inc_t *left_speed_pid,
                          pid_inc_t *right_speed_pid,
                          uint8_t *gray_trigger_streak)
{
    s_dcc_run_state = TASK_DCC_RUN_OFF;

    if (gray_trigger_streak != NULL)
    {
        *gray_trigger_streak = 0U;
    }

    dcc_stop_motor_now();
    dcc_reset_pid(pos_pid, left_speed_pid, right_speed_pid);
}

/**
 * @brief 进入 PREPARE 预备态。
 * @details
 * 1. 写入全局运行状态为 PREPARE。
 * 2. 预备态不允许输出电机命令，维持静止等待倒计时。
 * 3. 清空保护计数并复位 PID，避免切换态带入残留控制量。
 * @param pos_pid 位置环 PID 对象。
 * @param left_speed_pid 左轮速度环 PID 对象。
 * @param right_speed_pid 右轮速度环 PID 对象。
 * @param gray_trigger_streak 连续黑线计数器地址，可为 NULL。
 */
static void dcc_enter_prepare(pid_pos_t *pos_pid,
                              pid_inc_t *left_speed_pid,
                              pid_inc_t *right_speed_pid,
                              uint8_t *gray_trigger_streak)
{
    s_dcc_run_state = TASK_DCC_RUN_PREPARE;

    if (gray_trigger_streak != NULL)
    {
        *gray_trigger_streak = 0U;
    }

    dcc_stop_motor_now();
    dcc_reset_pid(pos_pid, left_speed_pid, right_speed_pid);
}

/**
 * @brief 进入 ON 运行态。
 * @details
 * 1. 写入全局运行状态为 ON。
 * 2. 清空黑线计数，保证 ON 态从干净状态开始统计保护条件。
 * 3. 先停机并复位 PID，随后由主循环按 mode 输出控制命令。
 * @param pos_pid 位置环 PID 对象。
 * @param left_speed_pid 左轮速度环 PID 对象。
 * @param right_speed_pid 右轮速度环 PID 对象。
 * @param gray_trigger_streak 连续黑线计数器地址，可为 NULL。
 */
static void dcc_enter_on(pid_pos_t *pos_pid,
                         pid_inc_t *left_speed_pid,
                         pid_inc_t *right_speed_pid,
                         uint8_t *gray_trigger_streak)
{
    s_dcc_run_state = TASK_DCC_RUN_ON;

    if (gray_trigger_streak != NULL)
    {
        *gray_trigger_streak = 0U;
    }

    dcc_stop_motor_now();
    dcc_reset_pid(pos_pid, left_speed_pid, right_speed_pid);
}

/**
 * @brief 进入 STOP 保护停机态。
 * @details
 * 1. 写入运行状态为 STOP，并强制 mode 回到 IDLE。
 * 2. 清空黑线计数，防止保护态退出后立即再次触发。
 * 3. 立即停机并复位 PID，阻断所有控制输出。
 * @param pos_pid 位置环 PID 对象。
 * @param left_speed_pid 左轮速度环 PID 对象。
 * @param right_speed_pid 右轮速度环 PID 对象。
 * @param gray_trigger_streak 连续黑线计数器地址，可为 NULL。
 */
static void dcc_enter_stop(pid_pos_t *pos_pid,
                           pid_inc_t *left_speed_pid,
                           pid_inc_t *right_speed_pid,
                           uint8_t *gray_trigger_streak)
{
    s_dcc_run_state = TASK_DCC_RUN_STOP;
    s_dcc_mode = TASK_DCC_MODE_IDLE;

    if (gray_trigger_streak != NULL)
    {
        *gray_trigger_streak = 0U;
    }

    dcc_stop_motor_now();
    dcc_reset_pid(pos_pid, left_speed_pid, right_speed_pid);
}

/**
 * @brief 判断 12 路循迹传感器中是否任意一路检测到黑线。
 * @return 任意一路有效返回 1，否则返回 0。
 */
static uint8_t dcc_has_any_black_line(void)
{
    uint8_t sensor_states[MOD_SENSOR_CHANNEL_NUM]; /* 12 路电平缓存 */
    uint8_t i;
    mod_sensor_ctx_t *sensor_ctx = mod_sensor_get_default_ctx();

    /* 读取失败时按“无黑线”处理，避免把通信异常误判为保护触发。 */
    if (!mod_sensor_get_states(sensor_ctx, sensor_states, MOD_SENSOR_CHANNEL_NUM))
    {
        return 0U;
    }

    for (i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++)
    {
        if (sensor_states[i] != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

/**
 * @brief 向 VOFA 发送 DCC 状态调试数据。
 * @details
 * 该函数受 `TASK_DCC_VOFA_ENABLE` 编译开关控制；关闭时会被编译为空操作。
 * @param mode 当前 DCC 模式。
 * @param run_state 当前 DCC 运行状态。
 * @param gray_streak 直线模式连续黑线计数。
 * @param track_weight 循迹模式最近有效权重。
 */
static void dcc_send_state_to_vofa(uint8_t mode, uint8_t run_state, uint8_t gray_streak, float track_weight)
{
#if (TASK_DCC_VOFA_ENABLE != 0U)
    mod_vofa_ctx_t *vofa_ctx = mod_vofa_get_default_ctx(); /* VOFA 默认上下文 */
    float payload[4];                                       /* [mode, run_state, gray_streak, track_weight] */

    if (!mod_vofa_is_bound(vofa_ctx))
    {
        return;
    }

    payload[0] = (float)mode;
    payload[1] = (float)run_state;
    payload[2] = (float)gray_streak;
    payload[3] = track_weight;
    (void)mod_vofa_send_float_ctx(vofa_ctx, TASK_DCC_VOFA_TAG, payload, 4U);
#else
    (void)mode;
    (void)run_state;
    (void)gray_streak;
    (void)track_weight;
#endif
}

/**
 * @brief 执行 STRAIGHT 模式控制分支。
 * @details
 * 1. 可选开启传感器保护窗：保护窗内忽略黑线急停判定。
 * 2. 保护窗外连续检测到黑线达到阈值后，切换到 STOP 保护态。
 * 3. 正常路径执行“位置外环 + 速度内环”，输出双轮占空比。
 * @param pos_pid 位置环 PID 对象。
 * @param left_speed_pid 左轮速度环 PID 对象。
 * @param right_speed_pid 右轮速度环 PID 对象。
 * @param gray_trigger_streak 连续黑线计数器地址。
 * @param sensor_guard_active 传感器保护窗使能标志。
 */
static void dcc_run_straight_mode(pid_pos_t *pos_pid,
                                  pid_inc_t *left_speed_pid,
                                  pid_inc_t *right_speed_pid,
                                  uint8_t *gray_trigger_streak,
                                  uint8_t sensor_guard_active)
{
    int64_t left_pos;            /* 左轮累计位置 */
    int64_t right_pos;           /* 右轮累计位置 */
    float pos_error;             /* 左右轮位置误差 */
    float outer_output;          /* 位置环输出，用于修正左右目标速度 */
    float left_target_speed;     /* 左轮速度环目标 */
    float right_target_speed;    /* 右轮速度环目标 */
    float left_feedback_speed;   /* 左轮速度反馈 */
    float right_feedback_speed;  /* 右轮速度反馈 */
    float left_duty_f;           /* 左轮占空比输出（浮点） */
    float right_duty_f;          /* 右轮占空比输出（浮点） */
    uint8_t has_black_line;      /* 本周期是否检测到黑线 */
    if ((pos_pid == NULL) || (left_speed_pid == NULL) || (right_speed_pid == NULL) || (gray_trigger_streak == NULL))
    {
        return;
    }

    if (sensor_guard_active != 0U)
    {
        /* 保护窗内：忽略传感器急停，计数归零 */
        *gray_trigger_streak = 0U;
    }
    else
    {
        /* 连续3次检测到任意黑线：进入STOP并回mode0 */
        has_black_line = dcc_has_any_black_line();
        if (has_black_line != 0U)
        {
            if (*gray_trigger_streak < 255U)
            {
                (*gray_trigger_streak)++;
            }
        }
        else
        {
            *gray_trigger_streak = 0U;
        }

        if (*gray_trigger_streak >= TASK_DCC_GRAY_STOP_COUNT)
        {
            dcc_enter_stop(pos_pid, left_speed_pid, right_speed_pid, gray_trigger_streak);
            return;
        }
    }

    /* 外环：根据左右轮位置误差计算速度修正量。 */
    left_pos = mod_motor_get_position(mod_motor_get_default_ctx(), MOD_MOTOR_LEFT);
    right_pos = mod_motor_get_position(mod_motor_get_default_ctx(), MOD_MOTOR_RIGHT);
    pos_error = (float)(left_pos - right_pos);

    outer_output = PID_Pos_Compute(pos_pid, -pos_error);
    outer_output = clamp_float(outer_output, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

    left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - outer_output);
    right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + outer_output);

    /* 内环：把左右目标速度转换为实际 duty。 */
    left_feedback_speed = (float)mod_motor_get_speed(mod_motor_get_default_ctx(), MOD_MOTOR_LEFT);
    right_feedback_speed = (float)mod_motor_get_speed(mod_motor_get_default_ctx(), MOD_MOTOR_RIGHT);

    PID_Inc_SetTarget(left_speed_pid, left_target_speed);
    PID_Inc_SetTarget(right_speed_pid, right_target_speed);
    left_duty_f = PID_Inc_Compute(left_speed_pid, left_feedback_speed);
    right_duty_f = PID_Inc_Compute(right_speed_pid, right_feedback_speed);

    mod_motor_set_duty(mod_motor_get_default_ctx(), MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
    mod_motor_set_duty(mod_motor_get_default_ctx(), MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));
}

/**
 * @brief 执行 TRACK 模式控制分支。
 * @details
 * 1. 当前周期有黑线时读取最新权重，并刷新“上次有效权重”缓存。
 * 2. 当前周期无黑线时优先使用上次有效权重，无历史值则退化为 0。
 * 3. 权重转换为速度修正量后进入双速度环，计算左右轮占空比输出。
 * @param left_speed_pid 左轮速度环 PID 对象。
 * @param right_speed_pid 右轮速度环 PID 对象。
 * @param last_valid_weight 上次有效权重缓存地址。
 * @param has_valid_weight 上次有效权重有效标志地址。
 */
static void dcc_run_track_mode(pid_inc_t *left_speed_pid,
                               pid_inc_t *right_speed_pid,
                               float *last_valid_weight,
                               uint8_t *has_valid_weight)
{
    float weight;                /* 本周期控制权重 */
    float correction;            /* 限幅后的速度修正量 */
    float left_target_speed;     /* 左轮目标速度 */
    float right_target_speed;    /* 右轮目标速度 */
    float left_feedback_speed;   /* 左轮反馈速度 */
    float right_feedback_speed;  /* 右轮反馈速度 */
    float left_duty_f;           /* 左轮占空比输出 */
    float right_duty_f;          /* 右轮占空比输出 */
    uint8_t has_black_line;      /* 是否检测到黑线 */
    mod_sensor_ctx_t *sensor_ctx = mod_sensor_get_default_ctx();
    if ((left_speed_pid == NULL) || (right_speed_pid == NULL) ||
        (last_valid_weight == NULL) || (has_valid_weight == NULL))
    {
        return;
    }

    has_black_line = dcc_has_any_black_line();
    if (has_black_line != 0U)
    {
        /* 本次读取到有效电平：刷新“上一次有效权重” */
        weight = mod_sensor_get_weight(sensor_ctx);
        *last_valid_weight = weight;
        *has_valid_weight = 1U;
    }
    else
    {
        /* 本次未读取到有效电平：沿用上一次有效权重 */
        if (*has_valid_weight != 0U)
        {
            weight = *last_valid_weight;
        }
        else
        {
            /* 尚无有效历史值时，退化为0 */
            weight = 0.0f;
        }
    }

    /* 权重转修正量后统一限幅，避免目标速度突变。 */
    correction = clamp_float(weight, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

    left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - correction);
    right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + correction);

    left_feedback_speed = (float)mod_motor_get_speed(mod_motor_get_default_ctx(), MOD_MOTOR_LEFT);
    right_feedback_speed = (float)mod_motor_get_speed(mod_motor_get_default_ctx(), MOD_MOTOR_RIGHT);

    PID_Inc_SetTarget(left_speed_pid, left_target_speed);
    PID_Inc_SetTarget(right_speed_pid, right_target_speed);
    left_duty_f = PID_Inc_Compute(left_speed_pid, left_feedback_speed);
    right_duty_f = PID_Inc_Compute(right_speed_pid, right_feedback_speed);

    mod_motor_set_duty(mod_motor_get_default_ctx(), MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
    mod_motor_set_duty(mod_motor_get_default_ctx(), MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));
}

/**
 * @brief 获取当前 DCC 模式。
 * @return 模式值：`IDLE/STRAIGHT/TRACK`。
 */
uint8_t task_dcc_get_mode(void)
{
    return s_dcc_mode;
}

/**
 * @brief 获取当前 DCC 运行状态。
 * @return 运行状态：`OFF/PREPARE/ON/STOP`。
 */
uint8_t task_dcc_get_run_state(void)
{
    return s_dcc_run_state;
}

/**
 * @brief 获取兼容态“是否就绪”标志。
 * @return 仅当运行状态为 `ON` 时返回 1，否则返回 0。
 */
uint8_t task_dcc_get_ready(void)
{
    return (uint8_t)(s_dcc_run_state == TASK_DCC_RUN_ON);
}

/**
 * @brief DCC 任务主循环：消费按键信号量并驱动 OFF/PREPARE/ON/STOP 状态机。
 * @param argument 任务参数（未使用）。
 */
void StartDccTask(void *argument)
{
    pid_pos_t pos_pid;                  /* 直线模式外环：位置环PID */
    pid_inc_t left_speed_pid;           /* 左轮速度环PID */
    pid_inc_t right_speed_pid;          /* 右轮速度环PID */
    uint8_t gray_trigger_streak = 0U;   /* 直线模式连续黑线计数 */
    uint32_t tick_freq;                 /* 内核tick频率 */
    uint32_t prepare_duration_tick;     /* PREPARE持续tick */
    uint32_t prepare_start_tick = 0U;   /* PREPARE开始tick */
    uint32_t straight_guard_tick;       /* 直线模式保护窗时长tick */
    uint32_t straight_guard_deadline = 0U; /* 直线模式保护窗结束tick */
    uint8_t straight_guard_armed = 0U;  /* 直线保护窗是否已启动 */
    float track_last_valid_weight = 0.0f; /* 循迹模式：上一次有效权重 */
    uint8_t track_has_valid_weight = 0U;  /* 循迹模式：是否已有有效权重 */

    (void)argument;

    /* 步骤0：等待 InitTask 完成模块绑定与上电初始化。 */
    task_wait_init_done();

#if (TASK_DCC_STARTUP_ENABLE == 0U)
    /* 启动开关关闭：挂起当前任务，底盘控制状态机不参与调度。 */
    (void)osThreadSuspend(osThreadGetId());
    for (;;)
    {
        osDelay(osWaitForever);
    }
#endif

    /* 步骤1：初始化位置环和双速度环 PID。 */
    PID_Pos_Init(&pos_pid,
                 MOTOR_POS_KP,
                 MOTOR_POS_KI,
                 MOTOR_POS_KD,
                 MOTOR_POS_OUTPUT_MAX,
                 MOTOR_POS_INTEGRAL_MAX);
    PID_Pos_SetTarget(&pos_pid, MOTOR_TARGET_ERROR);

    PID_Inc_Init(&left_speed_pid,
                 MOTOR_SPEED_KP,
                 MOTOR_SPEED_KI,
                 MOTOR_SPEED_KD,
                 (float)MOD_MOTOR_DUTY_MAX);

    PID_Inc_Init(&right_speed_pid,
                 MOTOR_SPEED_KP,
                 MOTOR_SPEED_KI,
                 MOTOR_SPEED_KD,
                 (float)MOD_MOTOR_DUTY_MAX);

    /* 步骤2：把毫秒级时序参数换算为 tick。 */
    tick_freq = osKernelGetTickFreq();
    prepare_duration_tick = dcc_ms_to_ticks(TASK_DCC_PREPARE_MS, tick_freq);
    straight_guard_tick = dcc_ms_to_ticks(TASK_DCC_STRAIGHT_GUARD_MS, tick_freq);

    /* 步骤3：上电缓冲后进入 OFF 安全态。 */
    osDelay(TASK_DCC_STARTUP_DELAY_MS);
    dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);

    for (;;)
    {
        uint32_t now_tick = osKernelGetTickCount();
        uint8_t straight_sensor_guard_active = 0U;

        /* 步骤4：消费三类控制信号量并驱动状态迁移。 */
        /* KEY2单击：mode与运行态全重置 */
        if (osSemaphoreAcquire(Sem_DccHandle, 0U) == osOK)
        {
            s_dcc_mode = TASK_DCC_MODE_IDLE;
            dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);
            prepare_start_tick = 0U;
            straight_guard_armed = 0U;
            straight_guard_deadline = 0U;
            track_last_valid_weight = 0.0f;
            track_has_valid_weight = 0U;

            /* 清理残留控制请求，避免重置后立即被旧事件拉走 */
            (void)osSemaphoreAcquire(Sem_TaskChangeHandle, 0U);
            (void)osSemaphoreAcquire(Sem_ReadyToggleHandle, 0U);
        }

        /* KEY3单击：切mode并回OFF */
        if (osSemaphoreAcquire(Sem_TaskChangeHandle, 0U) == osOK)
        {
            s_dcc_mode = (uint8_t)((s_dcc_mode + 1U) % TASK_DCC_MODE_TOTAL);
            dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);
            straight_guard_armed = 0U;
            straight_guard_deadline = 0U;
            track_last_valid_weight = 0.0f;
            track_has_valid_weight = 0U;
        }

        /* KEY3双击：OFF->PREPARE(仅mode!=0)，PREPARE/ON->OFF */
        if (osSemaphoreAcquire(Sem_ReadyToggleHandle, 0U) == osOK)
        {
            if (s_dcc_run_state == TASK_DCC_RUN_OFF)
            {
                if (s_dcc_mode != TASK_DCC_MODE_IDLE)
                {
                    dcc_enter_prepare(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);
                    prepare_start_tick = now_tick;
                    straight_guard_armed = 0U;
                    straight_guard_deadline = 0U;
                    track_last_valid_weight = 0.0f;
                    track_has_valid_weight = 0U;
                }
            }
            else if ((s_dcc_run_state == TASK_DCC_RUN_PREPARE) || (s_dcc_run_state == TASK_DCC_RUN_ON))
            {
                dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);
                straight_guard_armed = 0U;
                straight_guard_deadline = 0U;
                track_last_valid_weight = 0.0f;
                track_has_valid_weight = 0U;
            }
            else
            {
                /* STOP态下双击不直接进PREPARE */
            }
        }

        /* PREPARE到时自动进入ON */
        if (s_dcc_run_state == TASK_DCC_RUN_PREPARE)
        {
            if ((uint32_t)(now_tick - prepare_start_tick) >= prepare_duration_tick)
            {
                dcc_enter_on(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);
                straight_guard_armed = 0U;
                straight_guard_deadline = 0U;
                track_last_valid_weight = 0.0f;
                track_has_valid_weight = 0U;
            }
        }

        /* 步骤5：刷新底盘编码器反馈，供后续控制计算使用。 */
        mod_motor_tick(mod_motor_get_default_ctx());

        /* 非ON态：强制停机 */
        if (s_dcc_run_state != TASK_DCC_RUN_ON)
        {
            straight_guard_armed = 0U;
            straight_guard_deadline = 0U;
            track_last_valid_weight = 0.0f;
            track_has_valid_weight = 0U;
            dcc_stop_motor_now();
            osDelay(TASK_DCC_PERIOD_MS);
            continue;
        }

        /* 步骤6：ON 态下按 mode 执行对应控制分支。 */
        if (s_dcc_mode == TASK_DCC_MODE_IDLE)
        {
            gray_trigger_streak = 0U;
            straight_guard_armed = 0U;
            straight_guard_deadline = 0U;
            track_last_valid_weight = 0.0f;
            track_has_valid_weight = 0U;
            dcc_stop_motor_now();
        }
        else if (s_dcc_mode == TASK_DCC_MODE_STRAIGHT)
        {
            /* 直线模式首次进入ON时，开启1秒保护窗 */
            if (straight_guard_armed == 0U)
            {
                straight_guard_armed = 1U;
                straight_guard_deadline = now_tick + straight_guard_tick;
            }

            if (dcc_time_reached(now_tick, straight_guard_deadline) == 0)
            {
                straight_sensor_guard_active = 1U;
            }

            dcc_run_straight_mode(&pos_pid,
                                  &left_speed_pid,
                                  &right_speed_pid,
                                  &gray_trigger_streak,
                                  straight_sensor_guard_active);

            /* 非循迹模式下不保留循迹历史权重 */
            track_last_valid_weight = 0.0f;
            track_has_valid_weight = 0U;
        }
        else if (s_dcc_mode == TASK_DCC_MODE_TRACK)
        {
            gray_trigger_streak = 0U;
            straight_guard_armed = 0U;
            straight_guard_deadline = 0U;
            dcc_run_track_mode(&left_speed_pid,
                               &right_speed_pid,
                               &track_last_valid_weight,
                               &track_has_valid_weight);
        }
        else
        {
            straight_guard_armed = 0U;
            straight_guard_deadline = 0U;
            track_last_valid_weight = 0.0f;
            track_has_valid_weight = 0U;
            dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);
        }

        /* 步骤7：按开关可选输出 DCC 调试数据到 VOFA。 */
        dcc_send_state_to_vofa(s_dcc_mode,
                               s_dcc_run_state,
                               gray_trigger_streak,
                               track_last_valid_weight);

        osDelay(TASK_DCC_PERIOD_MS);
    }
}

