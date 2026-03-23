#include "task_stepper.h"
#include "task_dcc.h"
#include "task_init.h"
#include "mod_vofa.h"
#include "pid_pos.h"
#include "usart.h"
#include <math.h>
#include <string.h>

/* UART 鍙戦€佷簰鏂ラ攣锛堝湪 freertos.c 涓垱寤猴級 */
extern osMutexId_t PcMutexHandle;

typedef struct
{
    mod_stepper_ctx_t ctx;           /* 鍗忚灞備笂涓嬫枃 */
    uint8_t logic_id;                /* 閫昏緫杞?ID锛?=X锛?=Y */
    int32_t pos_pulse;               /* 浠诲姟灞備及绠椾綅缃紙鑴夊啿锛?*/
    int32_t limit_abs;               /* 杞檺浣嶇粷瀵瑰€硷紙姝ｆ暟锛?*/
    uint8_t dir_invert;              /* 鏂瑰悜鍙嶇浉 */
    pid_pos_t pos_pid;               /* 浣嶇疆 PID 锛堣緭鍑轰负姣忓懆鏈熻剦鍐插懡浠わ級 */

    uint8_t bound;                   /* 杞存槸鍚﹀凡缁戝畾鎴愬姛 */
    uint32_t last_pulse_cmd;         /* 涓婁竴鍛ㄦ湡鎸夐€熷害浼扮畻鐨勮剦鍐插閲?*/
    uint16_t last_speed_cmd;         /* 鏈€杩戜竴娆＄敓鏁堥€熷害鍛戒护锛坮pm锛?*/
    mod_stepper_dir_e last_dir_cmd;  /* 鏈€杩戜竴娆＄敓鏁堟柟鍚戝懡浠?*/
    uint8_t last_dir_valid;          /* last_dir_cmd 鏄惁鏈夋晥 */

    uint32_t cmd_ok_count;           /* 涓嬪彂鎴愬姛璁℃暟 */
    uint32_t cmd_drop_count;         /* 涓嬪彂澶辫触/涓㈠純璁℃暟 */
} task_stepper_axis_t;

static bool s_stepper_ready = false;
static task_stepper_state_t s_stepper_state;
static task_stepper_axis_t s_axis_x;
static task_stepper_axis_t s_axis_y;

/* 鍒ゆ柇鏌愰€昏緫杞存槸鍚﹀厑璁稿彂閫佹帶鍒跺懡浠わ紙鐢ㄤ簬鍗曡酱鑱旇皟锛?*/
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

/* 鍒濆鍖栧崟杞翠綅缃?PID 鍙傛暟 */
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

/* 鏍规嵁鈥滄瘡鍛ㄦ湡鍏佽鑴夊啿鏁扳€濆弽鎺ㄨ鍛ㄦ湡鍐呭彲鐢ㄩ€熷害锛堝悜涓婂彇鏁达級 */
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

/* 鎸夊綋鍓嶉€熷害浼扮畻鏈懆鏈熶綅绉昏剦鍐?*/
static uint32_t task_stepper_calc_cycle_pulse_from_speed(uint16_t speed_rpm)
{
    uint64_t numerator;
    uint32_t pulse_step;

    if (speed_rpm == 0U)
    {
        return 0U;
    }

    numerator = (uint64_t)speed_rpm * (uint64_t)TASK_STEPPER_PULSE_PER_REV * (uint64_t)TASK_STEPPER_PERIOD_MS;
    pulse_step = (uint32_t)((numerator + 30000ULL) / 60000ULL); /* 鍥涜垗浜斿叆 */
    if (pulse_step == 0U)
    {
        pulse_step = 1U;
    }

    return pulse_step;
}

/* 灏嗚宸鍙锋槧灏勪负鐢垫満鏂瑰悜涓庝綅缃鍙?*/
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

/* 鎸夊綋鍓嶆柟鍚戣绠楀墿浣欏彲杩愬姩鑴夊啿 */
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

/* 鍙戦€佸仠鏈哄懡浠わ紱鑻ユ湰鏉ュ氨鏄仠鏈虹姸鎬佸垯鍙洿鏂版湰鍦扮紦瀛?*/
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

/* 鍙戦€佷綅缃ā寮忓懡浠わ紝姣忔鍛戒护閮芥洿鏂颁换鍔″眰浼扮畻鐘舵€?*/
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

    /* 娴ｅ秶鐤嗗Ο鈥崇础娑撳绱濇稉宥呮缂侇參鈧喎瀹抽崨鎴掓姢閻樿埖鈧?*/
    axis->last_speed_cmd = 0U;
    axis->last_pulse_cmd = pulse_cmd;
    axis->last_dir_valid = 0U;
    axis->cmd_ok_count++;
    return 1U;
}

/* 鎸夆€滃綋鍓嶇敓鏁堥€熷害鍛戒护鈥濇帹杩涗綅缃及绠楋紝骞舵墽琛岄檺浣嶅仠鏈?*/
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

/* 鍗曡酱鎺у埗鍏ュ彛锛歑/Y 缁熶竴浣跨敤鈥滀綅缃?PID + 浣嶇疆鍛戒护鈥濊矾寰?*/
static void task_stepper_axis_control(task_stepper_axis_t *axis, int16_t err, uint8_t mode)
{
    uint32_t remain;
    uint32_t pulse_cmd;
    uint16_t speed_cmd;
    float pid_out;
    mod_stepper_dir_e dir_cmd;
    int32_t motion_sign = 1;
    int32_t new_pos;

    (void)mode;

    if ((axis == NULL) || (axis->bound == 0U))
    {
        return;
    }

    /* 杞磋缂栬瘧鏈熷紑鍏冲睆钄芥椂锛屼笉鍙戦€佷换浣曞懡浠?*/
    if (!task_stepper_is_axis_enabled(axis->logic_id))
    {
        return;
    }

    if ((err > -(int16_t)TASK_STEPPER_ERR_DEADBAND) &&
        (err < (int16_t)TASK_STEPPER_ERR_DEADBAND))
    {
        (void)task_stepper_axis_send_stop(axis);
        PID_Pos_Reset(&axis->pos_pid);
        return;
    }

    task_stepper_get_motion_direction(axis, err, &dir_cmd, &motion_sign);
    remain = task_stepper_axis_get_remain_pulse(axis, motion_sign);
    if (remain == 0U)
    {
        (void)task_stepper_axis_send_stop(axis);
        return;
    }

    /* 灏忚宸娇鐢ㄧ函 Kp锛屽ぇ璇樊浣跨敤 PI锛坕d=0锛?*/
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

    pulse_cmd = (uint32_t)(fabsf(pid_out) + 0.5f);
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

/* Y 杞村浐瀹氭祴璇曪細涓嶇湅璇樊锛屾瘡鍛ㄦ湡鍥哄畾涓嬪彂 50rpm/100pulse 鍛戒护 */
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

/* 绂诲紑鎺у埗鎬佹椂灏濊瘯鍋滄鍏ㄩ儴杞?*/
/* 閫€鍑烘帶鍒舵€佹椂缁熶竴鍋滄満锛涜灞忚斀杞村彧娓呯┖缂撳瓨涓嶄笅鍙戜覆鍙?*/
static void task_stepper_try_stop_all(void)
{
    /* X 杞村叧闂椂锛屼笉涓嬪彂 STOP锛屼粎娓呯┖浠诲姟灞傜紦瀛?*/
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
}

/* 浠?K230 璇诲彇鏈€鏂颁竴甯э紙鑻ユ湁锛?*/
/* 璇诲彇 K230 鏈€鏂颁竴甯ф暟鎹紱鏃犳柊甯ф椂杩斿洖 0 */
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

/* 鎸?K230 ID 鍒嗗彂璇樊鍒?X/Y 杞?*/
/* 灏?K230 鐨勪袱璺宸寜 id 鍒嗗彂鍒板搴旇酱鎺у埗鍣?*/
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
    /*
     * 涓存椂娴嬭瘯锛氬彧楠岃瘉 X 杞村搷搴旓紝鍏堝睆钄?Y 杞存帶鍒躲€?     * else if (s_stepper_state.motor1_id == TASK_STEPPER_AXIS_Y_ID)
     * {
     *     task_stepper_axis_control(&s_axis_y, s_stepper_state.err1, mode);
     * }
     */

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
    /*
     * 涓存椂娴嬭瘯锛氬彧楠岃瘉 X 杞村搷搴旓紝鍏堝睆钄?Y 杞存帶鍒躲€?     * else if (s_stepper_state.motor2_id == TASK_STEPPER_AXIS_Y_ID)
     * {
     *     task_stepper_axis_control(&s_axis_y, s_stepper_state.err2, mode);
     * }
     */
}

/* 鍛ㄦ湡涓婃姤璋冭瘯鏁版嵁鍒?VOFA锛堝綋鍓嶄粎 err1銆乪rr2锛?*/
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

/* 缁戝畾鍗曡酱涓插彛鍜岄┍鍔ㄥ湴鍧€ */
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

/* 鍒锋柊瀵瑰鍙鐘舵€佸揩鐓?*/
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
    task_stepper_axis_pid_init(&s_axis_x);

    s_axis_y.logic_id = TASK_STEPPER_AXIS_Y_ID;
    s_axis_y.limit_abs = TASK_STEPPER_Y_LIMIT_PULSE;
    s_axis_y.dir_invert = TASK_STEPPER_Y_DIR_INVERT;
    task_stepper_axis_pid_init(&s_axis_y);

    /* X 杞村彲閫氳繃缂栬瘧鏈熷紑鍏冲睆钄斤紝鐢ㄤ簬鍗曡酱涓插彛閾捐矾鑱旇皟 */
    if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_X_ID))
    {
        /* 涓存椂瀵硅皟锛歑 杞翠娇鐢?UART2 */
        x_ok = task_stepper_bind_axis(&s_axis_x, &huart5, TASK_STEPPER_X_DRIVER_ADDR);
    }
    else
    {
        x_ok = 1U;
        s_axis_x.bound = 0U;
    }

    if (task_stepper_is_axis_enabled(TASK_STEPPER_AXIS_Y_ID))
    {
        /* 涓存椂瀵硅皟锛歒 杞翠娇鐢?UART5 */
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

bool task_stepper_is_ready(void)
{
    return s_stepper_ready;
}

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

/* Stepper 涓讳换鍔★細璺熼殢 DCC 杩愯鎬侊紝鍦?ON 鎬佹寜甯ф帶鍒跺苟鍋氬け甯т繚鎶?*/
void StartStepperTask(void *argument)
{
    uint8_t dcc_mode;
    uint8_t dcc_run_state;
    uint8_t control_enable;
    uint8_t prev_control_enable = 0U;
    uint8_t no_frame_cycle_count = 0U;

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
            /* 杩涘叆 ON 鐨勯涓懆鏈熷厛鍙戜竴娆?STOP锛屾竻闄や笂涓€娆℃畫鐣欑姸鎬?*/
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
            no_frame_cycle_count = 0U;
        }

        if (control_enable != 0U)
        {
            if (task_stepper_update_latest_k230_frame() != 0U)
            {
                no_frame_cycle_count = 0U;
                task_stepper_apply_frame_control(dcc_mode);
            }
            else
            {
                /* 娌℃湁鏂板抚鏃讹紝鎸夊綋鍓嶉€熷害缁х画鎺ㄨ繘浼扮畻骞跺仛闄愪綅淇濇姢 */
                if (no_frame_cycle_count < 255U)
                {
                    no_frame_cycle_count++;
                }

                if (no_frame_cycle_count >= TASK_STEPPER_NO_FRAME_STOP_CYCLES)
                {
                    /* 杩炵画澶氬懆鏈熸棤鏂板抚锛氳Е鍙戝仠鏈轰繚鎶?*/
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
                    /* 鐭殏涓㈠抚瀹瑰繊锛氭寜涓婃閫熷害鐭椂鎺ㄨ繘浼扮畻浣嶇疆 */
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
                /* 涓存椂娴嬭瘯锛氬彧楠岃瘉 X 杞村搷搴旓紝鍏堝睆钄?Y 杞翠綅缃帹杩涖€?*/
            }

            /* Y 杞村浐瀹氭祴璇曟ā寮忥細姣忓懆鏈熷己鍒朵笅鍙戝浐瀹氫綅缃懡浠?*/
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
