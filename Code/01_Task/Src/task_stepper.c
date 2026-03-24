/**
 * @file    task_stepper.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   步进电机任务实现。
 * @details
 * 1. StepperTask 基于 DCC 运行态调度双轴视觉闭环控制。
 * 2. 任务从 mod_k230 获取最新误差帧，调用 mod_stepper 下发位置/停机命令。
 * 3. 内置软限位、短时丢帧容忍与连续丢帧保护停机，避免失控输出。
 * 4. 运行状态会周期发布到只读快照并通过 VOFA 输出联调数据。
 */
#include "task_stepper.h"
#include "task_dcc.h"
#include "task_init.h"
#include "mod_vofa.h"
#include "pid_pos.h"
#include "usart.h"
#include <math.h>
#include <string.h>

/* 串口发送互斥锁（在 freertos.c 中创建） */
extern osMutexId_t PcMutexHandle;
/* 单轴运行时状态与控制上下文 */
typedef struct
{
    mod_stepper_ctx_t ctx;           /* 步进协议上下文 */
    uint8_t logic_id;                /* 逻辑轴 ID：1=X，2=Y */
    int32_t pos_pulse;               /* 任务层估计位置（pulse） */
    int32_t limit_abs;               /* 软限位绝对值（正数） */
    uint8_t dir_invert;              /* 方向反相开关 */
    pid_pos_t pos_pid;               /* 位置 PID（输出单位：pulse/cycle） */

    uint8_t bound;                   /* 该轴是否绑定成功 */
    uint32_t last_pulse_cmd;         /* 最近一次下发的脉冲命令 */
    uint16_t last_speed_cmd;         /* 最近一次速度命令（rpm） */
    mod_stepper_dir_e last_dir_cmd;  /* 最近一次方向命令 */
    uint8_t last_dir_valid;          /* 最近方向命令是否有效 */

    uint32_t cmd_ok_count;           /* 命令成功计数 */
    uint32_t cmd_drop_count;         /* 命令失败/丢弃计数 */
} task_stepper_axis_t;

/* mode1（STRAIGHT）前馈状态：记录底盘编码器相邻采样差分 */
typedef struct
{
    uint8_t inited;        /* 是否已经完成首帧初始化 */
    int64_t left_prev_pos; /* 左电机上一次累计编码器值 */
    int64_t right_prev_pos;/* 右电机上一次累计编码器值 */
    int32_t left_delta;    /* 左电机本周期差分 */
    int32_t right_delta;   /* 右电机本周期差分 */
} task_stepper_mode1_ff_t;

/* 任务级静态状态 */
static bool s_stepper_ready = false; /* Stepper 任务初始化完成标志 */
static task_stepper_state_t s_stepper_state; /* 对外只读状态快照 */
static task_stepper_axis_t s_axis_x; /* X 轴运行时控制上下文 */
static task_stepper_axis_t s_axis_y; /* Y 轴运行时控制上下文 */
static task_stepper_mode1_ff_t s_mode1_ff; /* STRAIGHT 模式前馈缓存 */

/**
 * @brief 判断某逻辑轴当前是否允许下发控制命令。
 * @details
 * 该判断由编译期开关控制，用于单轴联调时屏蔽另一轴的串口命令输出。
 * @param logic_id 逻辑轴 ID。
 * @return 允许下发返回 true，否则返回 false。
 */
static bool task_stepper_is_axis_enabled(uint8_t logic_id)
{
    /* 逻辑轴开关用于联调阶段快速屏蔽单轴控制链路。 */
    if (logic_id == TASK_STEPPER_AXIS_X_ID)
    {
        return (TASK_STEPPER_ENABLE_X_AXIS != 0U);
    }
    if (logic_id == TASK_STEPPER_AXIS_Y_ID)
    {
        return (TASK_STEPPER_ENABLE_Y_AXIS != 0U);
    }
    return false;
}

/**
 * @brief 返回两个无符号整数中的较小值。
 * @param a 输入值 A。
 * @param b 输入值 B。
 * @return `a` 与 `b` 中较小者。
 */
static uint32_t task_stepper_min_u32(uint32_t a, uint32_t b)
{
    /* 用于脉冲命令与剩余行程的统一限幅。 */
    return (a < b) ? a : b;
}

/**
 * @brief 对 int32 值执行区间限幅。
 * @param value 待限幅值。
 * @param min 下限值。
 * @param max 上限值。
 * @return 限幅后的结果。
 */
static int32_t task_stepper_clamp_i32(int32_t value, int32_t min, int32_t max)
{
    /* 软限位位置保护统一入口。 */
    if (value < min)
    {
        return min;
    }
    if (value > max)
    {
        return max;
    }
    return value;
}

/**
 * @brief 将 int64 安全截断为 int32（饱和方式）。
 * @param value 待截断值。
 * @return 截断后结果，超范围时返回 `INT32_MAX/INT32_MIN`。
 */
static int32_t task_stepper_clamp_i64_to_i32(int64_t value)
{
    /* 编码器累计值差分可能超过 int32，需先做饱和截断。 */
    if (value > (int64_t)INT32_MAX)
    {
        return INT32_MAX;
    }
    if (value < (int64_t)INT32_MIN)
    {
        return INT32_MIN;
    }
    return (int32_t)value;
}

/**
 * @brief 复位 STRAIGHT 模式前馈缓存。
 * @details
 * 清空前馈内部状态后，下一周期会重新执行“首帧建基线”流程。
 */
static void task_stepper_mode1_ff_reset(void)
{
    /* 清空前馈缓存后，下一个周期会重新以“首帧”方式建立基线。 */
    memset(&s_mode1_ff, 0, sizeof(s_mode1_ff));
}

/**
 * @brief 更新 STRAIGHT 模式前馈输入。
 * @details
 * 1. 读取底盘左右轮编码器累计值。
 * 2. 首帧仅建立基线，不输出差分。
 * 3. 非首帧计算相邻周期差分，供 X/Y 轴前馈使用。
 */
static void task_stepper_mode1_ff_update(void)
{
    int64_t left_pos;  /* 左轮累计位置（来自 mod_motor） */
    int64_t right_pos; /* 右轮累计位置（来自 mod_motor） */

    /* 读取底盘左右轮累计编码器值，作为前馈输入基准。 */
    left_pos = mod_motor_get_position(mod_motor_get_default_ctx(), MOD_MOTOR_LEFT);
    right_pos = mod_motor_get_position(mod_motor_get_default_ctx(), MOD_MOTOR_RIGHT);

    if (s_mode1_ff.inited == 0U)
    {
        /* 首帧仅建立基线，不产生前馈差分。 */
        s_mode1_ff.left_prev_pos = left_pos;
        s_mode1_ff.right_prev_pos = right_pos;
        s_mode1_ff.left_delta = 0;
        s_mode1_ff.right_delta = 0;
        s_mode1_ff.inited = 1U;
        return;
    }

    /* 非首帧：计算相邻周期差分，供 X/Y 轴前馈计算使用。 */
    s_mode1_ff.left_delta = task_stepper_clamp_i64_to_i32(left_pos - s_mode1_ff.left_prev_pos);
    s_mode1_ff.right_delta = task_stepper_clamp_i64_to_i32(right_pos - s_mode1_ff.right_prev_pos);
    s_mode1_ff.left_prev_pos = left_pos;
    s_mode1_ff.right_prev_pos = right_pos;
}

/**
 * @brief 按逻辑轴获取前馈原始差分。
 * @details
 * 1. X 轴使用 `right_delta - left_delta`（偏转相关）。
 * 2. Y 轴使用 `(right_delta + left_delta) / 2`（平移相关）。
 * @param axis 目标轴对象。
 * @return 对应轴的原始差分值；参数无效返回 0。
 */
static int32_t task_stepper_mode1_ff_get_axis_delta(const task_stepper_axis_t *axis)
{
    if (axis == NULL)
    {
        return 0;
    }

    if (axis->logic_id == TASK_STEPPER_AXIS_X_ID)
    {
        return (s_mode1_ff.right_delta - s_mode1_ff.left_delta);
    }

    if (axis->logic_id == TASK_STEPPER_AXIS_Y_ID)
    {
        return (s_mode1_ff.right_delta + s_mode1_ff.left_delta) / 2;
    }

    return 0;
}

/**
 * @brief 计算 STRAIGHT 模式前馈输出并限幅。
 * @param axis 目标轴对象。
 * @return 当前轴前馈输出（pulse/cycle），未启用时返回 0。
 */
static float task_stepper_mode1_ff_get_output(const task_stepper_axis_t *axis)
{
#if (TASK_STEPPER_MODE1_FEEDFORWARD_ENABLE != 0U)
    float ff_out; /* 前馈输出（pulse/cycle） */
    float ff_k;   /* 当前轴前馈增益 */
    if (axis == NULL)
    {
        return 0.0f;
    }

    if (axis->logic_id == TASK_STEPPER_AXIS_X_ID)
    {
        ff_k = TASK_STEPPER_MODE1_FEEDFORWARD_X_K;
    }
    else if (axis->logic_id == TASK_STEPPER_AXIS_Y_ID)
    {
        ff_k = TASK_STEPPER_MODE1_FEEDFORWARD_Y_K;
    }
    else
    {
        return 0.0f;
    }

    /* 前馈 = 编码器差分 * 轴增益。 */
    ff_out = ((float)task_stepper_mode1_ff_get_axis_delta(axis)) * ff_k;
    if (ff_out > TASK_STEPPER_MODE1_FEEDFORWARD_MAX_PULSE)
    {
        ff_out = TASK_STEPPER_MODE1_FEEDFORWARD_MAX_PULSE;
    }
    else if (ff_out < -TASK_STEPPER_MODE1_FEEDFORWARD_MAX_PULSE)
    {
        ff_out = -TASK_STEPPER_MODE1_FEEDFORWARD_MAX_PULSE;
    }

    return ff_out;
#else
    (void)axis;
    return 0.0f;
#endif
}

/**
 * @brief 通过逻辑轴 ID 获取轴运行时对象。
 * @param logic_id 逻辑轴 ID。
 * @return 成功返回轴对象指针，失败返回 NULL。
 */
static task_stepper_axis_t *task_stepper_get_axis(uint8_t logic_id)
{
    if (logic_id == TASK_STEPPER_AXIS_X_ID)
    {
        return &s_axis_x;
    }
    if (logic_id == TASK_STEPPER_AXIS_Y_ID)
    {
        return &s_axis_y;
    }
    return NULL;
}

/**
 * @brief 初始化单轴位置 PID 参数。
 * @param axis 目标轴对象。
 */
static void task_stepper_axis_pid_init(task_stepper_axis_t *axis)
{
    if (axis == NULL)
    {
        return;
    }

    PID_Pos_Init(&axis->pos_pid,
                 TASK_STEPPER_POS_PID_KP,
                 TASK_STEPPER_POS_PID_KI,
                 TASK_STEPPER_POS_PID_KD,
                 TASK_STEPPER_POS_PID_OUT_MAX_PULSE,
                 TASK_STEPPER_POS_PID_INTEGRAL_MAX);
}

/**
 * @brief 由“每周期脉冲数”反推速度命令（rpm）。
 * @details
 * 1. 使用周期时基将 pulse/cycle 转换为 rpm。
 * 2. 采用向上取整策略，避免极小脉冲被量化为 0 rpm。
 * 3. 输出限制在 `TASK_STEPPER_POS_MAX_RPM` 内。
 * @param pulse 每周期脉冲数。
 * @return 反推速度命令（rpm）。
 */
static uint16_t task_stepper_calc_speed_from_pulse(uint32_t pulse)
{
    uint64_t numerator;   /* 分子：pulse * 60000 */
    uint32_t denominator; /* 分母：每圈脉冲 * 周期(ms) */
    uint32_t speed_req;   /* 反推速度请求值（rpm） */
    if (pulse == 0U)
    {
        return 0U;
    }

    numerator = (uint64_t)pulse * 60000ULL;
    denominator = (uint32_t)TASK_STEPPER_PULSE_PER_REV * (uint32_t)TASK_STEPPER_PERIOD_MS;
    if (denominator == 0U)
    {
        denominator = 1U;
    }

    speed_req = (uint32_t)((numerator + (uint64_t)denominator - 1ULL) / (uint64_t)denominator);
    if (speed_req == 0U)
    {
        speed_req = 1U;
    }
    if (speed_req > TASK_STEPPER_POS_MAX_RPM)
    {
        speed_req = TASK_STEPPER_POS_MAX_RPM;
    }

    return (uint16_t)speed_req;
}

/**
 * @brief 按当前速度估算本周期位移脉冲数。
 * @param speed_rpm 当前速度命令（rpm）。
 * @return 本周期等效脉冲数，最小为 1（当 speed_rpm 非 0）。
 */
static uint32_t task_stepper_calc_cycle_pulse_from_speed(uint16_t speed_rpm)
{
    uint64_t numerator; /* speed * pulse_per_rev * period_ms */
    uint32_t pulse_step;
    if (speed_rpm == 0U)
    {
        return 0U;
    }

    numerator = (uint64_t)speed_rpm * (uint64_t)TASK_STEPPER_PULSE_PER_REV * (uint64_t)TASK_STEPPER_PERIOD_MS;
    pulse_step = (uint32_t)((numerator + 30000ULL) / 60000ULL); /* 四舍五入 */
    if (pulse_step == 0U)
    {
        pulse_step = 1U;
    }

    return pulse_step;
}

/**
 * @brief 根据视觉误差符号确定电机方向（含反相配置）。
 * @param axis 目标轴对象。
 * @param err 当前视觉误差。
 * @param dir_out 输出方向命令。
 * @param motion_sign_out 输出方向符号（+1/-1）。
 */
static void task_stepper_get_motion_direction(task_stepper_axis_t *axis,
                                              int16_t err,
                                              mod_stepper_dir_e *dir_out,
                                              int32_t *motion_sign_out)
{
    int32_t sign; /* 方向符号：+1=CW，-1=CCW */
    if ((axis == NULL) || (dir_out == NULL) || (motion_sign_out == NULL))
    {
        return;
    }

    sign = (err >= 0) ? 1 : -1;
    if (axis->dir_invert != 0U)
    {
        sign = -sign;
    }

    *motion_sign_out = sign;
    *dir_out = (sign > 0) ? MOD_STEPPER_DIR_CW : MOD_STEPPER_DIR_CCW;
}

/**
 * @brief 根据控制输出符号确定电机方向（含反相配置）。
 * @param axis 目标轴对象。
 * @param output 控制输出值（pulse/cycle）。
 * @param dir_out 输出方向命令。
 * @param motion_sign_out 输出方向符号（+1/-1）。
 */
static void task_stepper_get_direction_from_output(task_stepper_axis_t *axis,
                                                   float output,
                                                   mod_stepper_dir_e *dir_out,
                                                   int32_t *motion_sign_out)
{
    int32_t sign; /* 由控制输出符号推导方向 */
    if ((axis == NULL) || (dir_out == NULL) || (motion_sign_out == NULL))
    {
        return;
    }

    sign = (output >= 0.0f) ? 1 : -1;
    if (axis->dir_invert != 0U)
    {
        sign = -sign;
    }

    *motion_sign_out = sign;
    *dir_out = (sign > 0) ? MOD_STEPPER_DIR_CW : MOD_STEPPER_DIR_CCW;
}

/**
 * @brief 按当前方向计算剩余可用行程（pulse）。
 * @param axis 目标轴对象。
 * @param motion_sign 方向符号（+1/-1）。
 * @return 当前方向剩余行程（pulse）。
 */
static uint32_t task_stepper_axis_get_remain_pulse(const task_stepper_axis_t *axis, int32_t motion_sign)
{
    int32_t remain; /* 当前方向上剩余可用行程 */
    if (axis == NULL)
    {
        return 0U;
    }

    if (motion_sign > 0)
    {
        remain = axis->limit_abs - axis->pos_pulse;
    }
    else
    {
        remain = axis->pos_pulse + axis->limit_abs;
    }

    if (remain <= 0)
    {
        return 0U;
    }

    return (uint32_t)remain;
}

/**
 * @brief 下发单轴停机命令。
 * @details
 * 1. 若轴已处于停止状态，仅清理任务层缓存并返回成功。
 * 2. 若需实际停机，则调用模块层 stop 接口下发命令。
 * 3. 维护命令成功/失败统计计数。
 * @param axis 目标轴对象。
 * @return 成功返回 1，失败返回 0。
 */
static uint8_t task_stepper_axis_send_stop(task_stepper_axis_t *axis)
{
    if ((axis == NULL) || (axis->bound == 0U))
    {
        return 0U;
    }

    if (axis->last_speed_cmd == 0U)
    {
        axis->last_pulse_cmd = 0U;
        axis->last_dir_valid = 0U;
        return 1U;
    }

    if (!mod_stepper_stop(&axis->ctx, false))
    {
        axis->cmd_drop_count++;
        return 0U;
    }

    axis->last_speed_cmd = 0U;
    axis->last_pulse_cmd = 0U;
    axis->last_dir_valid = 0U;
    axis->cmd_ok_count++;
    return 1U;
}

/**
 * @brief 下发单轴位置命令并更新任务层缓存。
 * @details
 * 1. 当 `pulse_cmd=0` 时退化为停机命令。
 * 2. 统一使用“相对位置 + 非同步”模式下发。
 * 3. 成功后刷新 last_* 缓存与统计计数。
 * @param axis 目标轴对象。
 * @param dir_cmd 方向命令。
 * @param speed_cmd 速度命令（rpm）。
 * @param pulse_cmd 脉冲命令。
 * @return 成功返回 1，失败返回 0。
 */
static uint8_t task_stepper_axis_send_position(task_stepper_axis_t *axis,
                                               mod_stepper_dir_e dir_cmd,
                                               uint16_t speed_cmd,
                                               uint32_t pulse_cmd)
{
    if ((axis == NULL) || (axis->bound == 0U))
    {
        return 0U;
    }

    if (pulse_cmd == 0U)
    {
        return task_stepper_axis_send_stop(axis);
    }

    if (speed_cmd > TASK_STEPPER_POS_MAX_RPM)
    {
        speed_cmd = TASK_STEPPER_POS_MAX_RPM;
    }

    /* 任务层统一下发“相对位置 + 非同步”命令。 */
    if (!mod_stepper_position(&axis->ctx, dir_cmd, speed_cmd, 0U, pulse_cmd, false, false))
    {
        axis->cmd_drop_count++;
        return 0U;
    }

    /* 当前位置模式以脉冲命令为准，last_speed/last_dir 置无效 */
    axis->last_speed_cmd = 0U;
    axis->last_pulse_cmd = pulse_cmd;
    axis->last_dir_valid = 0U;
    axis->cmd_ok_count++;
    return 1U;
}

/**
 * @brief 丢帧容忍阶段按上次速度推进位置估计。
 * @details
 * 1. 仅在存在有效历史速度与方向时推进估计位置。
 * 2. 推进前执行软限位剩余行程检查。
 * 3. 超出可用行程时触发停机并清理对应缓存。
 * @param axis 目标轴对象。
 */
static void task_stepper_axis_update_pos_by_last_velocity(task_stepper_axis_t *axis)
{
    int32_t motion_sign; /* 上一次速度命令对应的方向符号 */
    uint32_t remain;     /* 当前方向剩余行程 */
    uint32_t pulse_step; /* 由 last_speed 估算的本周期脉冲位移 */
    int32_t new_pos;     /* 估算更新后位置 */
    if (axis == NULL)
    {
        return;
    }

    if ((axis->last_speed_cmd == 0U) || (axis->last_dir_valid == 0U))
    {
        /* 无有效历史速度时，不推进位置估计。 */
        axis->last_pulse_cmd = 0U;
        return;
    }

    motion_sign = (axis->last_dir_cmd == MOD_STEPPER_DIR_CW) ? 1 : -1;
    remain = task_stepper_axis_get_remain_pulse(axis, motion_sign);
    if (remain == 0U)
    {
        (void)task_stepper_axis_send_stop(axis);
        axis->last_pulse_cmd = 0U;
        return;
    }

    pulse_step = task_stepper_calc_cycle_pulse_from_speed(axis->last_speed_cmd);
    pulse_step = task_stepper_min_u32(pulse_step, remain);

    new_pos = axis->pos_pulse + (motion_sign * (int32_t)pulse_step);
    axis->pos_pulse = task_stepper_clamp_i32(new_pos, -axis->limit_abs, axis->limit_abs);
    axis->last_pulse_cmd = pulse_step;
}

/**
 * @brief 单轴控制入口函数。
 * @details
 * 1. STRAIGHT 模式：PID 输出叠加前馈输出。
 * 2. TRACK 模式：仅使用 PID 输出并启用误差死区停机。
 * 3. 统一执行方向判定、软限位保护、速度反推与命令下发。
 * @param axis 目标轴对象。
 * @param err 当前轴视觉误差。
 * @param mode 当前 DCC 模式。
 */
static void task_stepper_axis_control(task_stepper_axis_t *axis, int16_t err, uint8_t mode)
{
    uint32_t remain;              /* 当前方向剩余可行程 */
    uint32_t pulse_cmd;           /* 本周期下发脉冲数 */
    uint16_t speed_cmd;           /* 本周期速度命令（rpm） */
    float pid_out;                /* PID 输出（pulse/cycle） */
    float ff_out = 0.0f;          /* 前馈输出（pulse/cycle） */
    float cmd_out;                /* 最终控制输出：PID(+FF) */
    mod_stepper_dir_e dir_cmd;    /* 电机方向命令 */
    int32_t motion_sign = 1;      /* 方向符号（+1/-1） */
    int32_t new_pos;              /* 新的任务层估计位置 */

    /* 步骤1：基本前提校验。 */
    if ((axis == NULL) || (axis->bound == 0U))
    {
        return;
    }

    /* 轴被编译期开关屏蔽时，不下发命令 */
    if (!task_stepper_is_axis_enabled(axis->logic_id))
    {
        return;
    }

    /* 小误差用纯 Kp，大误差用 PI（Kd=0） */
    /* 步骤2：小误差区采用纯 Kp，大误差区采用 PI。 */
    if ((err >= -(int16_t)TASK_STEPPER_KP_ONLY_ERR_THRESH) &&
        (err <= (int16_t)TASK_STEPPER_KP_ONLY_ERR_THRESH))
    {
        PID_Pos_Reset(&axis->pos_pid);
        pid_out = (float)err * TASK_STEPPER_KP_ONLY_KP;
    }
    else
    {
        PID_Pos_SetTarget(&axis->pos_pid, (float)err);
        pid_out = PID_Pos_Compute(&axis->pos_pid, 0.0f);
    }

    /* 步骤3：按模式决定是否叠加前馈输出。 */
    if ((mode == TASK_DCC_MODE_STRAIGHT) && (TASK_STEPPER_MODE1_FEEDFORWARD_ENABLE != 0U))
    {
        ff_out = task_stepper_mode1_ff_get_output(axis);
        cmd_out = pid_out + ff_out;
    }
    else
    {
        if ((err > -(int16_t)TASK_STEPPER_ERR_DEADBAND) &&
            (err < (int16_t)TASK_STEPPER_ERR_DEADBAND))
        {
            (void)task_stepper_axis_send_stop(axis);
            PID_Pos_Reset(&axis->pos_pid);
            return;
        }
        cmd_out = pid_out;
    }

    /* 步骤4：极小输出直接停机，避免高频抖动。 */
    if ((cmd_out > -0.5f) && (cmd_out < 0.5f))
    {
        (void)task_stepper_axis_send_stop(axis);
        return;
    }

    if ((mode == TASK_DCC_MODE_STRAIGHT) && (TASK_STEPPER_MODE1_FEEDFORWARD_ENABLE != 0U))
    {
        task_stepper_get_direction_from_output(axis, cmd_out, &dir_cmd, &motion_sign);
    }
    else
    {
        task_stepper_get_motion_direction(axis, err, &dir_cmd, &motion_sign);
    }

    /* 步骤5：计算软限位剩余行程，做命令限幅。 */
    remain = task_stepper_axis_get_remain_pulse(axis, motion_sign);
    if (remain == 0U)
    {
        (void)task_stepper_axis_send_stop(axis);
        return;
    }

    pulse_cmd = (uint32_t)(fabsf(cmd_out) + 0.5f);
    if (pulse_cmd == 0U)
    {
        pulse_cmd = 1U;
    }

    pulse_cmd = task_stepper_min_u32(pulse_cmd, remain);
    if (pulse_cmd == 0U)
    {
        (void)task_stepper_axis_send_stop(axis);
        return;
    }

    /* 步骤6：按目标脉冲反推速度并下发位置命令。 */
    speed_cmd = task_stepper_calc_speed_from_pulse(pulse_cmd);
    if (speed_cmd == 0U)
    {
        speed_cmd = 1U;
    }
    if (task_stepper_axis_send_position(axis, dir_cmd, speed_cmd, pulse_cmd) != 0U)
    {
        new_pos = axis->pos_pulse + (motion_sign * (int32_t)pulse_cmd);
        axis->pos_pulse = task_stepper_clamp_i32(new_pos, -axis->limit_abs, axis->limit_abs);
    }
}

/**
 * @brief Y 轴固定位置测试入口。
 * @details
 * 仅在 `TASK_STEPPER_Y_FIXED_POSITION_ENABLE` 打开时生效，每周期固定下发同一位置命令。
 */
static void task_stepper_run_y_fixed_position(void)
{
#if (TASK_STEPPER_Y_FIXED_POSITION_ENABLE != 0U)
    mod_stepper_dir_e dir_cmd; /* 固定测试方向 */
    if (!task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
    {
        return;
    }
    if (s_axis_y.bound == 0U)
    {
        return;
    }

    dir_cmd = (TASK_STEPPER_Y_FIXED_DIR_CW != 0U) ? MOD_STEPPER_DIR_CW : MOD_STEPPER_DIR_CCW;
    (void)task_stepper_axis_send_position(&s_axis_y,
                                          dir_cmd,
                                          TASK_STEPPER_Y_FIXED_SPEED_RPM,
                                          TASK_STEPPER_Y_FIXED_PULSE);
#endif
}

/**
 * @brief 退出控制态时统一执行双轴停机清理。
 * @details
 * 1. 对已启用轴下发 STOP 命令。
 * 2. 对禁用轴仅清理任务层缓存，不进行串口发送。
 * 3. 同步复位双轴 PID 与 mode1 前馈缓存。
 */
static void task_stepper_try_stop_all(void)
{
    /* X 轴：启用时下发 STOP，禁用时仅清缓存 */
    if ((s_axis_x.bound != 0U) && task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_X_ID))
    {
        (void)mod_stepper_stop(&s_axis_x.ctx, false);
    }
    s_axis_x.last_speed_cmd = 0U;
    s_axis_x.last_pulse_cmd = 0U;
    s_axis_x.last_dir_valid = 0U;
    PID_Pos_Reset(&s_axis_x.pos_pid);

    if ((s_axis_y.bound != 0U) && task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
    {
        (void)mod_stepper_stop(&s_axis_y.ctx, false);
    }
    s_axis_y.last_speed_cmd = 0U;
    s_axis_y.last_pulse_cmd = 0U;
    s_axis_y.last_dir_valid = 0U;
    PID_Pos_Reset(&s_axis_y.pos_pid);

    task_stepper_mode1_ff_reset();
}

/**
 * @brief 读取 K230 最新视觉帧并刷新任务状态快照。
 * @details
 * 1. 先刷新 K230 绑定状态，未绑定时直接返回无新帧。
 * 2. 读取成功后更新 `motor1/2_id` 与 `err1/2`。
 * 3. 无新帧时返回 0，由主循环执行丢帧策略。
 * @return 读取到新帧返回 1，否则返回 0。
 */
static uint8_t task_stepper_update_latest_k230_frame(void)
{
    mod_k230_ctx_t *k230_ctx;
    mod_k230_frame_data_t frame; /* 最新一帧临时缓存 */

    /* 每周期先刷新“通道是否绑定”状态，避免用旧状态误判。 */
    k230_ctx = mod_k230_get_default_ctx();
    s_stepper_state.k230_bound = mod_k230_is_bound(k230_ctx);
    if (!s_stepper_state.k230_bound)
    {
        return 0U;
    }

    /* 无新帧时返回 0，由主循环执行丢帧容错策略。 */
    if (!mod_k230_get_latest_frame(k230_ctx, &frame))
    {
        return 0U;
    }

    s_stepper_state.motor1_id = frame.motor1_id;
    s_stepper_state.err1 = frame.err1;
    s_stepper_state.motor2_id = frame.motor2_id;
    s_stepper_state.err2 = frame.err2;
    s_stepper_state.frame_update_count++;

    return 1U;
}

/**
 * @brief 将 K230 两路误差按 motor_id 分发到 X/Y 轴控制。
 * @param mode 当前 DCC 模式，用于控制分支选择。
 */
static void task_stepper_apply_frame_control(uint8_t mode)
{
    /* motor1/motor2 可分别映射到 X/Y 任意轴，按 id 动态分发。 */
    if ((s_stepper_state.motor1_id == TASK_STEPPER_AXIS_X_ID) &&
        task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_X_ID))
    {
        task_stepper_axis_control(&s_axis_x, s_stepper_state.err1, mode);
    }
    else if ((s_stepper_state.motor1_id == TASK_STEPPER_AXIS_Y_ID) &&
             task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
    {
#if (TASK_STEPPER_Y_FIXED_POSITION_ENABLE == 0U)
        task_stepper_axis_control(&s_axis_y, s_stepper_state.err1, mode);
#endif
    }
    if ((s_stepper_state.motor2_id == TASK_STEPPER_AXIS_X_ID) &&
        task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_X_ID))
    {
        task_stepper_axis_control(&s_axis_x, s_stepper_state.err2, mode);
    }
    else if ((s_stepper_state.motor2_id == TASK_STEPPER_AXIS_Y_ID) &&
             task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
    {
#if (TASK_STEPPER_Y_FIXED_POSITION_ENABLE == 0U)
        task_stepper_axis_control(&s_axis_y, s_stepper_state.err2, mode);
#endif
    }
}

/**
 * @brief 周期上报调试数据到 VOFA。
 * @details
 * 1. 该函数受 `TASK_STEPPER_VOFA_ENABLE` 开关控制。
 * 2. 开关开启时发送 `err1/err2` 并维护发送成功/失败计数。
 * 3. 开关关闭时仅更新状态，不执行发送动作。
 */
static void task_stepper_send_state_to_vofa(void)
{
#if (TASK_STEPPER_VOFA_ENABLE == 0U)
    s_stepper_state.vofa_bound = false;
    return;
#else
    mod_vofa_ctx_t *vofa_ctx;
    float err_payload[2]; /* [err1, err2] */
    vofa_ctx = mod_vofa_get_default_ctx();
    s_stepper_state.vofa_bound = mod_vofa_is_bound(vofa_ctx);
    if (!s_stepper_state.vofa_bound)
    {
        s_stepper_state.vofa_tx_drop_count++;
        return;
    }

    err_payload[0] = (float)s_stepper_state.err1;
    err_payload[1] = (float)s_stepper_state.err2;
    if (mod_vofa_send_float_ctx(vofa_ctx, TASK_STEPPER_VOFA_TAG, err_payload, 2U))
    {
        s_stepper_state.vofa_tx_ok_count++;
    }
    else
    {
        s_stepper_state.vofa_tx_drop_count++;
    }
#endif
}

/**
 * @brief 绑定单轴串口与驱动地址。
 * @param axis 目标轴对象。
 * @param huart 串口句柄。
 * @param driver_addr 驱动地址。
 * @return 绑定成功返回 1，否则返回 0。
 */
static uint8_t task_stepper_bind_axis(task_stepper_axis_t *axis, UART_HandleTypeDef *huart, uint8_t driver_addr)
{
    mod_stepper_bind_t bind; /* 驱动绑定参数 */
    uint8_t ok;              /* 绑定结果标记 */
    if ((axis == NULL) || (huart == NULL) || (driver_addr == 0U))
    {
        return 0U;
    }

    memset(&bind, 0, sizeof(bind));
    bind.huart = huart;
    bind.tx_mutex = PcMutexHandle;
    bind.driver_addr = driver_addr;

    if (!axis->ctx.inited)
    {
        ok = (uint8_t)mod_stepper_ctx_init(&axis->ctx, &bind);
    }
    else
    {
        ok = (uint8_t)mod_stepper_bind(&axis->ctx, &bind);
    }

    axis->bound = ok;
    return ok;
}

/**
 * @brief 刷新对外只读状态快照。
 * @param dcc_mode 当前 DCC 模式。
 * @param dcc_run_state 当前 DCC 运行状态。
 */
static void task_stepper_refresh_public_state(uint8_t dcc_mode, uint8_t dcc_run_state)
{
    /* 对外快照统一在此汇总，避免外部读取到中间态字段。 */
    s_stepper_state.x_axis_bound = (s_axis_x.bound != 0U);
    s_stepper_state.y_axis_bound = (s_axis_y.bound != 0U);
    s_stepper_state.dcc_mode = dcc_mode;
    s_stepper_state.dcc_run_state = dcc_run_state;

    s_stepper_state.x_pos_pulse = s_axis_x.pos_pulse;
    s_stepper_state.y_pos_pulse = s_axis_y.pos_pulse;
    s_stepper_state.x_busy = 0U;
    s_stepper_state.y_busy = 0U;

    s_stepper_state.x_last_pulse_cmd = s_axis_x.last_pulse_cmd;
    s_stepper_state.y_last_pulse_cmd = s_axis_y.last_pulse_cmd;
    s_stepper_state.x_last_speed_cmd = s_axis_x.last_speed_cmd;
    s_stepper_state.y_last_speed_cmd = s_axis_y.last_speed_cmd;

    s_stepper_state.x_cmd_ok_count = s_axis_x.cmd_ok_count;
    s_stepper_state.y_cmd_ok_count = s_axis_y.cmd_ok_count;
    s_stepper_state.x_cmd_drop_count = s_axis_x.cmd_drop_count;
    s_stepper_state.y_cmd_drop_count = s_axis_y.cmd_drop_count;
}

/**
 * @brief 初始化 Stepper 双轴通道。
 * @details
 * 1. 清空状态快照和双轴运行时状态。
 * 2. 配置双轴软限位、方向反相与 PID 参数。
 * 3. 按工程映射绑定串口（X=huart5，Y=huart2）。
 * 4. 写入 `configured/ready` 状态供主循环和外部查询使用。
 * @return 当前使能轴全部准备成功返回 true，否则返回 false。
 */
bool task_stepper_prepare_channels(void)
{
    uint8_t x_ok; /* X 轴绑定结果 */
    uint8_t y_ok; /* Y 轴绑定结果 */
    memset(&s_stepper_state, 0, sizeof(s_stepper_state));
    memset(&s_axis_x, 0, sizeof(s_axis_x));
    memset(&s_axis_y, 0, sizeof(s_axis_y));
    task_stepper_mode1_ff_reset();

    s_axis_x.logic_id = TASK_STEPPER_AXIS_X_ID;
    s_axis_x.limit_abs = TASK_STEPPER_X_LIMIT_PULSE;
    s_axis_x.dir_invert = TASK_STEPPER_X_DIR_INVERT;
    task_stepper_axis_pid_init(&s_axis_x);

    s_axis_y.logic_id = TASK_STEPPER_AXIS_Y_ID;
    s_axis_y.limit_abs = TASK_STEPPER_Y_LIMIT_PULSE;
    s_axis_y.dir_invert = TASK_STEPPER_Y_DIR_INVERT;
    task_stepper_axis_pid_init(&s_axis_y);

    /* X 轴可通过编译期开关屏蔽，用于单轴串口联调 */
    if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_X_ID))
    {
        /* 当前工程映射：X 轴使用 huart5 */
        x_ok = task_stepper_bind_axis(&s_axis_x, &huart5, TASK_STEPPER_X_DRIVER_ADDR);
    }
    else
    {
        x_ok = 1U;
        s_axis_x.bound = 0U;
    }

    if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
    {
        /* 当前工程映射：Y 轴使用 huart2 */
        y_ok = task_stepper_bind_axis(&s_axis_y, &huart2, TASK_STEPPER_Y_DRIVER_ADDR);
    }
    else
    {
        y_ok = 1U;
        s_axis_y.bound = 0U;
    }

    s_stepper_state.x_axis_bound = (s_axis_x.bound != 0U);
    s_stepper_state.y_axis_bound = (s_axis_y.bound != 0U);
    s_stepper_state.configured = (uint8_t)((x_ok != 0U) && (y_ok != 0U));

    s_stepper_ready = true;
    return (s_stepper_state.configured != 0U);
}

/**
 * @brief 查询 Stepper 任务是否已完成初始化。
 * @return 初始化完成返回 true，否则返回 false。
 */
bool task_stepper_is_ready(void)
{
    return s_stepper_ready;
}

/**
 * @brief 向指定轴发送速度模式命令（带状态检查和速度限幅）。
 * @param logic_id 逻辑轴 ID（`1`=X，`2`=Y）。
 * @param dir 目标方向。
 * @param vel_rpm 目标速度（rpm）。
 * @param acc 加速度参数（透传给协议层）。
 * @param sync_flag 是否启用同步执行标志。
 * @return 发送成功返回 `true`，否则返回 `false`。
 */
bool task_stepper_send_velocity(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag)
{
    task_stepper_axis_t *axis = task_stepper_get_axis(logic_id);
    uint16_t speed_limited = vel_rpm;
    if (!task_stepper_is_axis_enabled(logic_id))
    {
        return false;
    }

    if ((axis == NULL) || (axis->bound == 0U))
    {
        return false;
    }

    if (speed_limited > TASK_STEPPER_VEL_MAX_RPM)
    {
        speed_limited = TASK_STEPPER_VEL_MAX_RPM;
    }

    return mod_stepper_velocity(&axis->ctx, dir, speed_limited, acc, sync_flag);
}

/**
 * @brief 向指定轴发送位置模式命令（带状态检查和速度限幅）。
 * @param logic_id 逻辑轴 ID（`1`=X，`2`=Y）。
 * @param dir 目标方向。
 * @param vel_rpm 目标速度（rpm）。
 * @param acc 加速度参数（透传给协议层）。
 * @param pulse 本次位置命令脉冲数。
 * @param absolute_mode 是否绝对位置模式。
 * @param sync_flag 是否启用同步执行标志。
 * @return 发送成功返回 `true`，否则返回 `false`。
 */
bool task_stepper_send_position(uint8_t logic_id,
                                mod_stepper_dir_e dir,
                                uint16_t vel_rpm,
                                uint8_t acc,
                                uint32_t pulse,
                                bool absolute_mode,
                                bool sync_flag)
{
    task_stepper_axis_t *axis = task_stepper_get_axis(logic_id);
    uint16_t speed_limited = vel_rpm;
    if (!task_stepper_is_axis_enabled(logic_id))
    {
        return false;
    }

    if ((axis == NULL) || (axis->bound == 0U))
    {
        return false;
    }

    if (speed_limited > TASK_STEPPER_POS_MAX_RPM)
    {
        speed_limited = TASK_STEPPER_POS_MAX_RPM;
    }

    return mod_stepper_position(&axis->ctx, dir, speed_limited, acc, pulse, absolute_mode, sync_flag);
}

/**
 * @brief Stepper 主任务入口。
 * @details
 * 1. 根据 DCC 运行态启停双轴闭环控制链路。
 * 2. 控制使能时处理视觉帧、执行轴控制与丢帧容错策略。
 * 3. 控制禁用或退出时执行统一停机清理。
 * 4. 周期发布只读状态并发送 VOFA 调试数据。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartStepperTask(void *argument)
{
    uint8_t dcc_mode;                     /* 当前 DCC 模式 */
    uint8_t dcc_run_state;                /* 当前 DCC 运行状态 */
    uint8_t control_enable;               /* 当前周期是否允许闭环控制 */
    uint8_t prev_control_enable = 0U;     /* 上周期控制使能状态 */
    uint8_t prev_dcc_mode = TASK_DCC_MODE_IDLE; /* 上周期 DCC 模式 */
    uint8_t no_frame_cycle_count = 0U;    /* 连续无新帧计数 */

    (void)argument;

    /* 步骤0：等待 InitTask 完成资源注入，避免串口/互斥锁未就绪。 */
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
        /* 若 InitTask 阶段未预绑定，这里兜底完成通道准备。 */
        (void)task_stepper_prepare_channels();
    }

    for (;;)
    {
        dcc_mode = task_dcc_get_mode();
        dcc_run_state = task_dcc_get_run_state();

        /* 步骤1：驱动协议栈泵函数，处理发送状态机与接收缓存。 */
        if (s_axis_x.bound != 0U)
        {
            mod_stepper_process(&s_axis_x.ctx);
        }
        if (s_axis_y.bound != 0U)
        {
            mod_stepper_process(&s_axis_y.ctx);
        }

        control_enable = (uint8_t)((dcc_run_state == TASK_DCC_RUN_ON) &&
                                   ((dcc_mode == TASK_DCC_MODE_STRAIGHT) || (dcc_mode == TASK_DCC_MODE_TRACK)));

        /* 步骤2：控制链路 OFF->ON 边沿，先做一次清状态停机。 */
        if ((control_enable != 0U) && (prev_control_enable == 0U))
        {
            /* 进入 ON 的首个周期：先发送 STOP 清理残留状态 */
            if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_X_ID))
            {
                (void)task_stepper_axis_send_stop(&s_axis_x);
                PID_Pos_Reset(&s_axis_x.pos_pid);
            }
            if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
            {
                (void)task_stepper_axis_send_stop(&s_axis_y);
                PID_Pos_Reset(&s_axis_y.pos_pid);
            }
            task_stepper_mode1_ff_reset();
            no_frame_cycle_count = 0U;
        }

        /* 步骤3：控制使能时执行闭环；否则执行退出态清理。 */
        if (control_enable != 0U)
        {
            if (dcc_mode != prev_dcc_mode)
            {
                task_stepper_mode1_ff_reset();
            }

            if (dcc_mode == TASK_DCC_MODE_STRAIGHT)
            {
                task_stepper_mode1_ff_update();
            }

            /* 步骤3.1：有新帧则正常闭环；无新帧则进入丢帧策略。 */
            if (task_stepper_update_latest_k230_frame() != 0U)
            {
                no_frame_cycle_count = 0U;
                task_stepper_apply_frame_control(dcc_mode);
            }
            else
            {
                /* 无新帧：按当前策略容忍丢帧并进行限位保护 */
                if (no_frame_cycle_count < 255U)
                {
                    no_frame_cycle_count++;
                }

                if (no_frame_cycle_count >= TASK_STEPPER_NO_FRAME_STOP_CYCLES)
                {
                    /* 连续多周期无新帧：触发保护停机 */
                    if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_X_ID))
                    {
                        (void)task_stepper_axis_send_stop(&s_axis_x);
                        PID_Pos_Reset(&s_axis_x.pos_pid);
                    }
                    if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
                    {
#if (TASK_STEPPER_Y_FIXED_POSITION_ENABLE == 0U)
                        (void)task_stepper_axis_send_stop(&s_axis_y);
                        PID_Pos_Reset(&s_axis_y.pos_pid);
#endif
                    }
                }
                else
                {
                    /* 短时丢帧容忍：按上次速度推进位置估计 */
                    if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_X_ID))
                    {
                        task_stepper_axis_update_pos_by_last_velocity(&s_axis_x);
                    }
                    if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
                    {
#if (TASK_STEPPER_Y_FIXED_POSITION_ENABLE == 0U)
                        task_stepper_axis_update_pos_by_last_velocity(&s_axis_y);
#endif
                    }
                }
            }

            /* Y 轴固定测试模式：每周期强制下发固定位置命令 */
            task_stepper_run_y_fixed_position();
        }
        else if (prev_control_enable != 0U)
        {
            no_frame_cycle_count = 0U;
            task_stepper_try_stop_all();
        }
        else
        {
            no_frame_cycle_count = 0U;
        }

        prev_dcc_mode = dcc_mode;
        prev_control_enable = control_enable;

        /* 步骤4：发布状态快照并上报调试数据。 */
        task_stepper_refresh_public_state(dcc_mode, dcc_run_state);
        task_stepper_send_state_to_vofa();

        osDelay(TASK_STEPPER_PERIOD_MS);
    }
}

/**
 * @brief 获取 Stepper 只读状态快照。
 * @param logic_id 预留参数（当前未使用）。
 * @return 任务就绪后返回状态快照指针，否则返回 NULL。
 */
const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id)
{
    (void)logic_id;

    if (!s_stepper_ready)
    {
        return NULL;
    }

    return &s_stepper_state;
}

