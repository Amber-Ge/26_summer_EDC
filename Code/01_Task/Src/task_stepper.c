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
    pid_pos_t pos_pid;               /* 位置 PID（输出：每周期脉冲数） */

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
static bool s_stepper_ready = false;
static task_stepper_state_t s_stepper_state;
static task_stepper_axis_t s_axis_x;
static task_stepper_axis_t s_axis_y;
static task_stepper_mode1_ff_t s_mode1_ff;

/* 判断某逻辑轴是否允许下发控制命令（单轴联调时可屏蔽） */
static bool task_stepper_is_axis_enabled(uint8_t logic_id)
{
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

/* 返回两个无符号数中的较小值 */
static uint32_t task_stepper_min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

/* 有符号整型限幅 */
static int32_t task_stepper_clamp_i32(int32_t value, int32_t min, int32_t max)
{
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

/* int64 到 int32 的安全截断 */
static int32_t task_stepper_clamp_i64_to_i32(int64_t value)
{
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

/* 复位 mode1 前馈缓存 */
static void task_stepper_mode1_ff_reset(void)
{
    memset(&s_mode1_ff, 0, sizeof(s_mode1_ff));
}

/* 更新 mode1 前馈输入：读取底盘双电机编码器差分 */
static void task_stepper_mode1_ff_update(void)
{
    int64_t left_pos;
    int64_t right_pos;

    left_pos = mod_motor_get_position(MOD_MOTOR_LEFT);
    right_pos = mod_motor_get_position(MOD_MOTOR_RIGHT);

    if (s_mode1_ff.inited == 0U)
    {
        s_mode1_ff.left_prev_pos = left_pos;
        s_mode1_ff.right_prev_pos = right_pos;
        s_mode1_ff.left_delta = 0;
        s_mode1_ff.right_delta = 0;
        s_mode1_ff.inited = 1U;
        return;
    }

    s_mode1_ff.left_delta = task_stepper_clamp_i64_to_i32(left_pos - s_mode1_ff.left_prev_pos);
    s_mode1_ff.right_delta = task_stepper_clamp_i64_to_i32(right_pos - s_mode1_ff.right_prev_pos);
    s_mode1_ff.left_prev_pos = left_pos;
    s_mode1_ff.right_prev_pos = right_pos;
}

/* 按轴获取前馈原始差分
 * X 轴：右-左（偏转相关）
 * Y 轴：(左+右)/2（平移相关）
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

/* 计算 mode1 前馈输出并限幅（单位：pulse/cycle） */
static float task_stepper_mode1_ff_get_output(const task_stepper_axis_t *axis)
{
#if (TASK_STEPPER_MODE1_FEEDFORWARD_ENABLE != 0U)
    float ff_out;
    float ff_k;

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

/* 通过逻辑 ID 获取轴对象 */
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

/* 初始化单轴位置 PID 参数 */
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

/* 由“每周期脉冲数”反推速度命令（rpm，向上取整） */
static uint16_t task_stepper_calc_speed_from_pulse(uint32_t pulse)
{
    uint64_t numerator;
    uint32_t denominator;
    uint32_t speed_req;

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

/* 按当前速度估算本周期位移脉冲数 */
static uint32_t task_stepper_calc_cycle_pulse_from_speed(uint16_t speed_rpm)
{
    uint64_t numerator;
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

/* 根据视觉误差确定电机方向（含反相） */
static void task_stepper_get_motion_direction(task_stepper_axis_t *axis,
                                              int16_t err,
                                              mod_stepper_dir_e *dir_out,
                                              int32_t *motion_sign_out)
{
    int32_t sign;

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

/* 根据“控制输出符号”确定电机方向（含反相） */
static void task_stepper_get_direction_from_output(task_stepper_axis_t *axis,
                                                   float output,
                                                   mod_stepper_dir_e *dir_out,
                                                   int32_t *motion_sign_out)
{
    int32_t sign;

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

/* 按当前方向计算可用剩余行程（pulse） */
static uint32_t task_stepper_axis_get_remain_pulse(const task_stepper_axis_t *axis, int32_t motion_sign)
{
    int32_t remain;

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

/* 下发停机命令；若本来就停止，则只清本地缓存 */
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

/* 下发位置命令，并更新任务层本地状态 */
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

/* 在短时无新帧时，按“上次有效速度”推进位置估计并做限位保护 */
static void task_stepper_axis_update_pos_by_last_velocity(task_stepper_axis_t *axis)
{
    int32_t motion_sign;
    uint32_t remain;
    uint32_t pulse_step;
    int32_t new_pos;

    if (axis == NULL)
    {
        return;
    }

    if ((axis->last_speed_cmd == 0U) || (axis->last_dir_valid == 0U))
    {
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

/* 单轴控制入口：
 * mode1（STRAIGHT）：PID + 前馈
 * mode2（TRACK）：PID
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

/* Y 轴固定位置测试：忽略误差，每周期下发固定位置命令 */
static void task_stepper_run_y_fixed_position(void)
{
#if (TASK_STEPPER_Y_FIXED_POSITION_ENABLE != 0U)
    mod_stepper_dir_e dir_cmd;

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

/* 退出控制态时统一停机；被屏蔽轴仅清本地缓存，不下发串口 */
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

/* 读取 K230 最新一帧；无新帧返回 0 */
static uint8_t task_stepper_update_latest_k230_frame(void)
{
    mod_k230_ctx_t *k230_ctx;
    mod_k230_frame_data_t frame;

    k230_ctx = mod_k230_get_default_ctx();
    s_stepper_state.k230_bound = mod_k230_is_bound(k230_ctx);
    if (!s_stepper_state.k230_bound)
    {
        return 0U;
    }

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

/* 将 K230 两路误差按 motor_id 分发到 X/Y 轴控制 */
static void task_stepper_apply_frame_control(uint8_t mode)
{
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

/* 周期上报调试数据到 VOFA（当前仅发送 err1/err2） */
static void task_stepper_send_state_to_vofa(void)
{
    mod_vofa_ctx_t *vofa_ctx;
    float err_payload[2];

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
}

/* 绑定单轴串口与驱动地址 */
static uint8_t task_stepper_bind_axis(task_stepper_axis_t *axis, UART_HandleTypeDef *huart, uint8_t driver_addr)
{
    mod_stepper_bind_t bind;
    uint8_t ok;

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

/* 刷新对外只读状态快照 */
static void task_stepper_refresh_public_state(uint8_t dcc_mode, uint8_t dcc_run_state)
{
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

/* 初始化 Stepper 任务通道：
 * 1) 清状态
 * 2) 配置 X/Y 轴限位、反相和 PID
 * 3) 绑定对应串口
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

/* 查询 Stepper 任务是否已完成初始化 */
bool task_stepper_is_ready(void)
{
    return s_stepper_ready;
}

/* 对外速度命令接口：检查轴状态并做速度限幅 */
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

/* 对外位置命令接口：检查轴状态并做速度限幅 */
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

/* Stepper 主任务：
 * 1) 跟随 DCC 运行状态
 * 2) ON 且 mode1/mode2 时执行视觉闭环
 * 3) 丢帧时执行短时容忍与保护停机
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
        dcc_mode = task_dcc_get_mode();
        dcc_run_state = task_dcc_get_run_state();

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

        task_stepper_refresh_public_state(dcc_mode, dcc_run_state);
        task_stepper_send_state_to_vofa();

        osDelay(TASK_STEPPER_PERIOD_MS);
    }
}

/* 获取只读状态快照（logic_id 当前保留） */
const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id)
{
    (void)logic_id;

    if (!s_stepper_ready)
    {
        return NULL;
    }

    return &s_stepper_state;
}
