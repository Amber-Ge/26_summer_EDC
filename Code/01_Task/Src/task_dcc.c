/**
 * @file    task_dcc.c
 * @brief   DCC 控制任务实现。
 * @details
 * 1. 文件作用：实现 DCC 控制流程、周期更新与任务内状态管理。
 * 2. 上下层绑定：上层由 FreeRTOS 调度执行；下层调用 Module/Driver 层执行硬件动作。
 */
#include "task_dcc.h"
#include "task_init.h"
#include <stddef.h>

#define TASK_DCC_GRAY_STOP_COUNT (3U)      /* 直线模式：连续黑线触发阈值 */
#define TASK_DCC_STARTUP_DELAY_MS (3000U)  /* 上电后任务额外延时，避免启动瞬态 */
#define TASK_DCC_STRAIGHT_GUARD_MS (1000U) /* 直线模式启动后1秒传感器保护窗 */

/* 当前模式：0=IDLE, 1=STRAIGHT, 2=TRACK */
static volatile uint8_t s_dcc_mode = TASK_DCC_MODE_IDLE; // DCC 当前工作模式状态。
/* 当前运行状态：OFF / PREPARE / ON / STOP */
static volatile uint8_t s_dcc_run_state = TASK_DCC_RUN_OFF; // DCC 输出启停状态。

/* 浮点限幅 */
/**
 * @brief 对浮点数执行区间限幅。
 * @param value 待限幅值。
 * @param min 下限值。
 * @param max 上限值。
 * @return 限幅后的结果。
 */
static float clamp_float(float value, float min, float max)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    float result = value; // 执行状态变量

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

/* 浮点占空比转整型命令（含限幅与四舍五入） */
/**
 * @brief 将浮点占空比换算为电机占空比整型命令。
 * @param duty_f 目标占空比（浮点）。
 * @return 已限幅并四舍五入后的占空比命令。
 */
static int16_t convert_to_duty_cmd(float duty_f)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    float duty_limited; // 控制量变量

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

/* 立即停止双电机 */
/**
 * @brief 立即停止左右电机输出。
 * @param 无。
 * @return 无。
 */
static void dcc_stop_motor_now(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    mod_motor_set_duty(MOD_MOTOR_LEFT, 0);
    mod_motor_set_duty(MOD_MOTOR_RIGHT, 0);
}

/* 统一复位PID内部状态 */
/**
 * @brief 重置位置环与左右速度环 PID 内部状态并恢复目标误差。
 * @param pos_pid 位置环 PID 对象。
 * @param left_speed_pid 左轮速度环 PID 对象。
 * @param right_speed_pid 右轮速度环 PID 对象。
 * @return 无。
 */
static void dcc_reset_pid(pid_pos_t *pos_pid, pid_inc_t *left_speed_pid, pid_inc_t *right_speed_pid)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if ((pos_pid == NULL) || (left_speed_pid == NULL) || (right_speed_pid == NULL))
    {
        return;
    }

    PID_Pos_Reset(pos_pid);
    PID_Inc_Reset(left_speed_pid);
    PID_Inc_Reset(right_speed_pid);
    PID_Pos_SetTarget(pos_pid, MOTOR_TARGET_ERROR);
}

/* 毫秒转tick，向上取整，且最少为1 tick */
/**
 * @brief 将毫秒时间换算为 RTOS tick，向上取整且至少为 1 tick。
 * @param duration_ms 目标时长（毫秒）。
 * @param tick_freq RTOS tick 频率（Hz），传入 0 时按 1000Hz 兜底。
 * @return 换算后的 tick 数。
 */
static uint32_t dcc_ms_to_ticks(uint32_t duration_ms, uint32_t tick_freq)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint32_t ticks; // 局部业务变量

    if (tick_freq == 0U)
    {
        tick_freq = 1000U;
    }

    ticks = (uint32_t)(((uint64_t)duration_ms * (uint64_t)tick_freq + 999ULL) / 1000ULL);
    if (ticks == 0U)
    {
        ticks = 1U;
    }

    return ticks;
}

/* Tick比较：now是否到达target（支持回绕） */
/**
 * @brief 判断当前 tick 是否达到目标 tick（支持回绕）。
 * @param now_tick 当前 tick。
 * @param target_tick 目标 tick。
 * @return 达到返回 1，否则返回 0。
 */
static int dcc_time_reached(uint32_t now_tick, uint32_t target_tick)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return ((int32_t)(now_tick - target_tick) >= 0);
}

/* 进入OFF：停机 + PID复位 + 黑线计数清零 */
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

/* 进入PREPARE：等待3秒，不输出电机 */
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

/* 进入ON：允许按当前mode执行控制 */
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

/* 进入STOP：停机保护态，同时mode回到0 */
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

/* 读取12路灰度状态，判断是否任意一路检测到黑线 */
/**
 * @brief 判断 12 路循迹传感器中是否任意一路检测到黑线。
 * @param 无。
 * @return 任意一路有效返回 1，否则返回 0。
 */
static uint8_t dcc_has_any_black_line(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint8_t sensor_states[MOD_SENSOR_CHANNEL_NUM]; // 传感器状态缓存数组
    uint8_t i; // 循环或计数变量

    if (!mod_sensor_get_states(sensor_states, MOD_SENSOR_CHANNEL_NUM))
    {
        return 0U;
    }

    for (i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++) // 循环计数器
    {
        if (sensor_states[i] != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

/* 直线模式控制：位置环 + 速度环；支持1秒传感器保护窗 */
static void dcc_run_straight_mode(pid_pos_t *pos_pid,
                                  pid_inc_t *left_speed_pid,
                                  pid_inc_t *right_speed_pid,
                                  uint8_t *gray_trigger_streak,
                                  uint8_t sensor_guard_active)
{
    int64_t left_pos; // 控制量变量
    int64_t right_pos; // 控制量变量
    float pos_error; // 执行状态变量
    float outer_output; // 局部业务变量
    float left_target_speed; // 控制量变量
    float right_target_speed; // 控制量变量
    float left_feedback_speed; // 控制量变量
    float right_feedback_speed; // 控制量变量
    float left_duty_f; // 控制量变量
    float right_duty_f; // 控制量变量
    uint8_t has_black_line; // 局部业务变量

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

    left_pos = mod_motor_get_position(MOD_MOTOR_LEFT);
    right_pos = mod_motor_get_position(MOD_MOTOR_RIGHT);
    pos_error = (float)(left_pos - right_pos);

    outer_output = PID_Pos_Compute(pos_pid, -pos_error);
    outer_output = clamp_float(outer_output, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

    left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - outer_output);
    right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + outer_output);

    left_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_LEFT);
    right_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_RIGHT);

    PID_Inc_SetTarget(left_speed_pid, left_target_speed);
    PID_Inc_SetTarget(right_speed_pid, right_target_speed);
    left_duty_f = PID_Inc_Compute(left_speed_pid, left_feedback_speed);
    right_duty_f = PID_Inc_Compute(right_speed_pid, right_feedback_speed);

    mod_motor_set_duty(MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
    mod_motor_set_duty(MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));
}

/* 循迹模式控制：按weight分配左右目标速度 + 速度环 */
static void dcc_run_track_mode(pid_inc_t *left_speed_pid,
                               pid_inc_t *right_speed_pid,
                               float *last_valid_weight,
                               uint8_t *has_valid_weight)
{
    float weight; // 局部业务变量
    float correction; // 局部业务变量
    float left_target_speed; // 控制量变量
    float right_target_speed; // 控制量变量
    float left_feedback_speed; // 控制量变量
    float right_feedback_speed; // 控制量变量
    float left_duty_f; // 控制量变量
    float right_duty_f; // 控制量变量
    uint8_t has_black_line; // 局部业务变量

    if ((left_speed_pid == NULL) || (right_speed_pid == NULL) ||
        (last_valid_weight == NULL) || (has_valid_weight == NULL))
    {
        return;
    }

    has_black_line = dcc_has_any_black_line();
    if (has_black_line != 0U)
    {
        /* 本次读取到有效电平：刷新“上一次有效权重” */
        weight = mod_sensor_get_weight();
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

    correction = clamp_float(weight, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

    left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - correction);
    right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + correction);

    left_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_LEFT);
    right_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_RIGHT);

    PID_Inc_SetTarget(left_speed_pid, left_target_speed);
    PID_Inc_SetTarget(right_speed_pid, right_target_speed);
    left_duty_f = PID_Inc_Compute(left_speed_pid, left_feedback_speed);
    right_duty_f = PID_Inc_Compute(right_speed_pid, right_feedback_speed);

    mod_motor_set_duty(MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
    mod_motor_set_duty(MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));
}

/**
 * @brief 获取当前 DCC 模式。
 * @param 无。
 * @return 模式值：`IDLE/STRAIGHT/TRACK`。
 */
uint8_t task_dcc_get_mode(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return s_dcc_mode;
}

/**
 * @brief 获取当前 DCC 运行状态。
 * @param 无。
 * @return 运行状态：`OFF/PREPARE/ON/STOP`。
 */
uint8_t task_dcc_get_run_state(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return s_dcc_run_state;
}

/**
 * @brief 获取兼容态“是否就绪”标志。
 * @param 无。
 * @return 仅当运行状态为 `ON` 时返回 1，否则返回 0。
 */
uint8_t task_dcc_get_ready(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return (uint8_t)(s_dcc_run_state == TASK_DCC_RUN_ON);
}

/**
 * @brief DCC 任务主循环：消费按键信号量并驱动 OFF/PREPARE/ON/STOP 状态机。
 * @param argument 任务参数（未使用）。
 * @return 无。
 */
void StartDccTask(void *argument)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
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

    task_wait_init_done();

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

    tick_freq = osKernelGetTickFreq();
    prepare_duration_tick = dcc_ms_to_ticks(TASK_DCC_PREPARE_MS, tick_freq);
    straight_guard_tick = dcc_ms_to_ticks(TASK_DCC_STRAIGHT_GUARD_MS, tick_freq);

    osDelay(TASK_DCC_STARTUP_DELAY_MS);
    dcc_enter_off(&pos_pid, &left_speed_pid, &right_speed_pid, &gray_trigger_streak);

    for (;;) // 循环计数器
    {
        uint32_t now_tick = osKernelGetTickCount(); // 局部业务变量
        uint8_t straight_sensor_guard_active = 0U; // 数据缓存变量

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

        mod_motor_tick();

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

        osDelay(TASK_DCC_PERIOD_MS);
    }
}



