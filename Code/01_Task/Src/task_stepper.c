#include "task_stepper.h"
#include "task_dcc.h"
#include "task_init.h"
#include "mod_vofa.h"
#include "usart.h"
#include <string.h>

/* UART 发送互斥锁（在 freertos.c 中创建） */
extern osMutexId_t PcMutexHandle;

typedef struct
{
    mod_stepper_ctx_t ctx;           /* 协议层上下文 */
    uint8_t logic_id;                /* 逻辑轴 ID：1=X，2=Y */
    int32_t pos_pulse;               /* 任务层估算位置（脉冲） */
    int32_t limit_abs;               /* 软限位绝对值（正数） */
    uint8_t dir_invert;              /* 方向反相 */

    uint8_t bound;                   /* 轴是否已绑定成功 */
    uint32_t last_pulse_cmd;         /* 上一周期按速度估算的脉冲增量 */
    uint16_t last_speed_cmd;         /* 最近一次生效速度命令（rpm） */
    mod_stepper_dir_e last_dir_cmd;  /* 最近一次生效方向命令 */
    uint8_t last_dir_valid;          /* last_dir_cmd 是否有效 */

    uint32_t cmd_ok_count;           /* 下发成功计数 */
    uint32_t cmd_drop_count;         /* 下发失败/丢弃计数 */
} task_stepper_axis_t;

static bool s_stepper_ready = false;
static task_stepper_state_t s_stepper_state;
static task_stepper_axis_t s_axis_x;
static task_stepper_axis_t s_axis_y;

static uint32_t task_stepper_min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

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

static uint32_t task_stepper_abs_i16(int16_t value)
{
    if (value >= 0)
    {
        return (uint32_t)value;
    }
    return (uint32_t)(-value);
}

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

static float task_stepper_get_err2vel_k(uint8_t mode)
{
    if (mode == TASK_DCC_MODE_STRAIGHT)
    {
        return TASK_STEPPER_ERR2VEL_K_MODE1;
    }
    return TASK_STEPPER_ERR2VEL_K_MODE2;
}

/* 根据“每周期允许脉冲数”反推该周期内可用速度（向上取整） */
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
    if (speed_req > TASK_STEPPER_VEL_MAX_RPM)
    {
        speed_req = TASK_STEPPER_VEL_MAX_RPM;
    }

    return (uint16_t)speed_req;
}

/* 按当前速度估算本周期位移脉冲 */
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

/* 将误差符号映射为电机方向与位置符号 */
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

/* 按当前方向计算剩余可运动脉冲 */
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

static uint8_t task_stepper_axis_send_velocity(task_stepper_axis_t *axis, mod_stepper_dir_e dir_cmd, uint16_t speed_cmd)
{
    if ((axis == NULL) || (axis->bound == 0U))
    {
        return 0U;
    }

    if (speed_cmd == 0U)
    {
        return task_stepper_axis_send_stop(axis);
    }

    if ((axis->last_speed_cmd == speed_cmd) &&
        (axis->last_dir_valid != 0U) &&
        (axis->last_dir_cmd == dir_cmd))
    {
        return 1U;
    }

    if (!mod_stepper_velocity(&axis->ctx, dir_cmd, speed_cmd, 0U, false))
    {
        axis->cmd_drop_count++;
        return 0U;
    }

    axis->last_speed_cmd = speed_cmd;
    axis->last_dir_cmd = dir_cmd;
    axis->last_dir_valid = 1U;
    axis->cmd_ok_count++;
    return 1U;
}

/* 按“当前生效速度命令”推进位置估算，并执行限位停机 */
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

/* 速度模式控制：误差 -> 速度，带死区、限速、软限位 */
static void task_stepper_axis_control(task_stepper_axis_t *axis, int16_t err, uint8_t mode)
{
    uint32_t abs_err;
    uint32_t remain;
    uint16_t speed_cmd;
    uint16_t speed_limit_by_remain;
    float k_value;
    mod_stepper_dir_e dir_cmd;
    int32_t motion_sign = 1;

    if ((axis == NULL) || (axis->bound == 0U))
    {
        return;
    }

    if ((err > -(int16_t)TASK_STEPPER_ERR_DEADBAND) &&
        (err < (int16_t)TASK_STEPPER_ERR_DEADBAND))
    {
        (void)task_stepper_axis_send_stop(axis);
        task_stepper_axis_update_pos_by_last_velocity(axis);
        return;
    }

    k_value = task_stepper_get_err2vel_k(mode);
    if (k_value < 0.0f)
    {
        k_value = -k_value;
    }

    abs_err = task_stepper_abs_i16(err);
    speed_cmd = (uint16_t)((float)abs_err * k_value + 0.5f);
    if (speed_cmd > TASK_STEPPER_VEL_MAX_RPM)
    {
        speed_cmd = TASK_STEPPER_VEL_MAX_RPM;
    }

    task_stepper_get_motion_direction(axis, err, &dir_cmd, &motion_sign);
    remain = task_stepper_axis_get_remain_pulse(axis, motion_sign);
    if (remain == 0U)
    {
        speed_cmd = 0U;
    }
    else
    {
        speed_limit_by_remain = task_stepper_calc_speed_from_pulse(remain);
        if ((speed_limit_by_remain != 0U) && (speed_cmd > speed_limit_by_remain))
        {
            speed_cmd = speed_limit_by_remain;
        }
    }

    (void)task_stepper_axis_send_velocity(axis, dir_cmd, speed_cmd);
    task_stepper_axis_update_pos_by_last_velocity(axis);
}

/* 离开控制态时尝试停止全部轴 */
static void task_stepper_try_stop_all(void)
{
    if (s_axis_x.bound != 0U)
    {
        (void)mod_stepper_stop(&s_axis_x.ctx, false);
        s_axis_x.last_speed_cmd = 0U;
        s_axis_x.last_pulse_cmd = 0U;
        s_axis_x.last_dir_valid = 0U;
    }
    if (s_axis_y.bound != 0U)
    {
        (void)mod_stepper_stop(&s_axis_y.ctx, false);
        s_axis_y.last_speed_cmd = 0U;
        s_axis_y.last_pulse_cmd = 0U;
        s_axis_y.last_dir_valid = 0U;
    }
}

/* 从 K230 读取最新一帧（若有） */
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

/* 按 K230 ID 分发误差到 X/Y 轴 */
static void task_stepper_apply_frame_control(uint8_t mode)
{
    if (s_stepper_state.motor1_id == TASK_STEPPER_AXIS_X_ID)
    {
        task_stepper_axis_control(&s_axis_x, s_stepper_state.err1, mode);
    }
    else if (s_stepper_state.motor1_id == TASK_STEPPER_AXIS_Y_ID)
    {
        task_stepper_axis_control(&s_axis_y, s_stepper_state.err1, mode);
    }

    if (s_stepper_state.motor2_id == TASK_STEPPER_AXIS_X_ID)
    {
        task_stepper_axis_control(&s_axis_x, s_stepper_state.err2, mode);
    }
    else if (s_stepper_state.motor2_id == TASK_STEPPER_AXIS_Y_ID)
    {
        task_stepper_axis_control(&s_axis_y, s_stepper_state.err2, mode);
    }
}

static void task_stepper_send_state_to_vofa(void)
{
    mod_vofa_ctx_t *vofa_ctx;
    float payload[10];

    vofa_ctx = mod_vofa_get_default_ctx();
    s_stepper_state.vofa_bound = mod_vofa_is_bound(vofa_ctx);
    if (!s_stepper_state.vofa_bound)
    {
        s_stepper_state.vofa_tx_drop_count++;
        return;
    }

    payload[0] = (float)s_stepper_state.err1;
    payload[1] = (float)s_stepper_state.err2;
    payload[2] = (float)s_axis_x.pos_pulse;
    payload[3] = (float)s_axis_y.pos_pulse;
    payload[4] = (float)s_axis_x.last_pulse_cmd;
    payload[5] = (float)s_axis_y.last_pulse_cmd;
    payload[6] = (float)s_axis_x.last_speed_cmd;
    payload[7] = (float)s_axis_y.last_speed_cmd;
    payload[8] = 0.0f;
    payload[9] = 0.0f;

    if (mod_vofa_send_float_ctx(vofa_ctx, TASK_STEPPER_VOFA_TAG, payload, 10U))
    {
        s_stepper_state.vofa_tx_ok_count++;
    }
    else
    {
        s_stepper_state.vofa_tx_drop_count++;
    }
}

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

bool task_stepper_prepare_channels(void)
{
    uint8_t x_ok;
    uint8_t y_ok;

    memset(&s_stepper_state, 0, sizeof(s_stepper_state));
    memset(&s_axis_x, 0, sizeof(s_axis_x));
    memset(&s_axis_y, 0, sizeof(s_axis_y));

    s_axis_x.logic_id = TASK_STEPPER_AXIS_X_ID;
    s_axis_x.limit_abs = TASK_STEPPER_X_LIMIT_PULSE;
    s_axis_x.dir_invert = TASK_STEPPER_X_DIR_INVERT;

    s_axis_y.logic_id = TASK_STEPPER_AXIS_Y_ID;
    s_axis_y.limit_abs = TASK_STEPPER_Y_LIMIT_PULSE;
    s_axis_y.dir_invert = TASK_STEPPER_Y_DIR_INVERT;

    x_ok = task_stepper_bind_axis(&s_axis_x, &huart5, TASK_STEPPER_X_DRIVER_ADDR);
    y_ok = task_stepper_bind_axis(&s_axis_y, &huart2, TASK_STEPPER_Y_DRIVER_ADDR);

    s_stepper_state.x_axis_bound = (x_ok != 0U);
    s_stepper_state.y_axis_bound = (y_ok != 0U);
    s_stepper_state.configured = (uint8_t)((x_ok != 0U) && (y_ok != 0U));

    s_stepper_ready = true;
    return (s_stepper_state.configured != 0U);
}

bool task_stepper_is_ready(void)
{
    return s_stepper_ready;
}

bool task_stepper_send_velocity(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag)
{
    task_stepper_axis_t *axis = task_stepper_get_axis(logic_id);
    uint16_t speed_limited = vel_rpm;

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

    if ((axis == NULL) || (axis->bound == 0U))
    {
        return false;
    }

    if (speed_limited > TASK_STEPPER_VEL_MAX_RPM)
    {
        speed_limited = TASK_STEPPER_VEL_MAX_RPM;
    }

    return mod_stepper_position(&axis->ctx, dir, speed_limited, acc, pulse, absolute_mode, sync_flag);
}

void StartStepperTask(void *argument)
{
    uint8_t dcc_mode;
    uint8_t dcc_run_state;
    uint8_t control_enable;
    uint8_t prev_control_enable = 0U;

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

        mod_stepper_process(&s_axis_x.ctx);
        mod_stepper_process(&s_axis_y.ctx);

        control_enable = (uint8_t)((dcc_run_state == TASK_DCC_RUN_ON) &&
                                   ((dcc_mode == TASK_DCC_MODE_STRAIGHT) || (dcc_mode == TASK_DCC_MODE_TRACK)));

        if (control_enable != 0U)
        {
            if (task_stepper_update_latest_k230_frame() != 0U)
            {
                task_stepper_apply_frame_control(dcc_mode);
            }
            else
            {
                /* 没有新帧时，按当前速度继续推进估算并做限位保护 */
                task_stepper_axis_update_pos_by_last_velocity(&s_axis_x);
                task_stepper_axis_update_pos_by_last_velocity(&s_axis_y);
            }
        }
        else if (prev_control_enable != 0U)
        {
            task_stepper_try_stop_all();
        }

        prev_control_enable = control_enable;

        task_stepper_refresh_public_state(dcc_mode, dcc_run_state);
        task_stepper_send_state_to_vofa();

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
