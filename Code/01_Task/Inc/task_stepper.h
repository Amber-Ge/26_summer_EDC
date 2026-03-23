/**
 ******************************************************************************
 * @file    task_stepper.h
 * @brief   Stepper 浠诲姟灞傛帴鍙ｏ細璇诲彇 K230 璇樊骞舵帶鍒?X/Y 姝ヨ繘鐢垫満
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_STEPPER_H
#define FINAL_GRADUATE_WORK_TASK_STEPPER_H

#include "cmsis_os.h"
#include "mod_k230.h"
#include "mod_stepper.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 浠诲姟璋冨害鍙傛暟 ========================= */

/* Stepper 浠诲姟鍛ㄦ湡锛坢s锛?*/
#define TASK_STEPPER_PERIOD_MS (20U)

/* 杩炵画澶氬皯涓懆鏈熸敹涓嶅埌鏂板抚鍚庤Е鍙戝仠鏈轰繚鎶?*/
#define TASK_STEPPER_NO_FRAME_STOP_CYCLES (3U)

/* Stepper 浠诲姟鍚姩寮€鍏筹細1=姝ｅ父杩愯锛?=鍚姩鍚庢寕璧?*/
#define TASK_STEPPER_STARTUP_ENABLE (1U)

/* 鍙戦€佸埌 VOFA 鐨勬洸绾挎爣绛撅紙褰撳墠浠呭彂 err1銆乪rr2锛?*/
#define TASK_STEPPER_VOFA_TAG ("StepperErr")

/* ========================= 杞?ID 瀹氫箟 ========================= */

/* 涓?K230 鍗忚绾﹀畾锛歩d=1 涓?X 杞达紝id=2 涓?Y 杞?*/
#define TASK_STEPPER_AXIS_X_ID (1U)
#define TASK_STEPPER_AXIS_Y_ID (2U)

/* 杞存帶鍒跺紑鍏筹細鐢ㄤ簬浠诲姟灞傚揩閫熷睆钄芥煇涓€杞村懡浠ゅ彂閫?*/
#define TASK_STEPPER_ENABLE_X_AXIS (1U)
#define TASK_STEPPER_ENABLE_Y_AXIS (1U)

/* ========================= 涓插彛涓庨┍鍔ㄥ湴鍧€ ========================= */

/* 鎸夊綋鍓嶇‖浠堕厤缃紝涓や釜椹卞姩鍦板潃閮戒娇鐢?1 */
#define TASK_STEPPER_X_DRIVER_ADDR (1U)
#define TASK_STEPPER_Y_DRIVER_ADDR (1U)

/* ========================= 鎺у埗鍙傛暟锛堝彲璋冿級 ========================= */

/* 16 缁嗗垎涓嬶紝姣忓湀鑴夊啿鏁帮紙200 * 16 = 3200锛?*/
#define TASK_STEPPER_PULSE_PER_REV (3200U)

/* 閫熷害妯″紡鏈€澶ц浆閫燂紙rpm锛?*/
#define TASK_STEPPER_VEL_MAX_RPM (50U)

/* 浣嶇疆妯″紡鏈€澶ц浆閫燂紙rpm锛?*/
#define TASK_STEPPER_POS_MAX_RPM (50U)

/* 浣嶇疆 PI 鍙傛暟锛氳緭鍑烘槸鈥滄瘡鍛ㄦ湡鑴夊啿鈥?*/
#define TASK_STEPPER_POS_PID_KP (0.20f)
#define TASK_STEPPER_POS_PID_KI (0.0012f)
#define TASK_STEPPER_POS_PID_KD (0.00f)

/* PID 杈撳嚭闄愬箙锛堝崟鍛ㄦ湡鏈€澶ц剦鍐插懡浠わ級 */
#define TASK_STEPPER_POS_PID_OUT_MAX_PULSE (53.0f)

/* PID 绉垎闄愬箙 */
#define TASK_STEPPER_POS_PID_INTEGRAL_MAX (6000.0f)

/* 灏忚宸鍖猴細|err| < deadband 鏃跺仠鏈?*/
#define TASK_STEPPER_ERR_DEADBAND (2)

/* 灏忚寖鍥磋宸細|err| <= 10 鏃朵粎鐢?Kp锛屽叧闂?I */
#define TASK_STEPPER_KP_ONLY_ERR_THRESH (10)

/* 灏忚寖鍥村崟鐙?Kp 瀵瑰簲鐨勮剦鍐插鐩婏紙pulse/cycle per err锛?*/
#define TASK_STEPPER_KP_ONLY_KP (0.12f)

/* 澶у皬璇樊閮戒娇鐢ㄤ綅缃?PID锛屼笉鍐嶅垏鎹㈤€熷害妯″紡 */

/* Y 杞村浐瀹氭祴璇曟ā寮忥細寮€鍚悗涓嶅啀浣跨敤璇樊锛屾瘡鍛ㄦ湡涓嬪彂鍥哄畾浣嶇疆鍛戒护 */
#define TASK_STEPPER_Y_FIXED_POSITION_ENABLE (0U)
#define TASK_STEPPER_Y_FIXED_SPEED_RPM (50U)
#define TASK_STEPPER_Y_FIXED_PULSE (100U)
#define TASK_STEPPER_Y_FIXED_DIR_CW (1U)

/* X 杞磋蒋闄愪綅鑼冨洿锛歔-800, +800]锛屽叏琛岀▼ 1600 鑴夊啿 */
#define TASK_STEPPER_X_LIMIT_PULSE (800)

/* Y 杞磋蒋闄愪綅鑼冨洿锛歔-400, +400]锛屽叏琛岀▼ 800 鑴夊啿 */
#define TASK_STEPPER_Y_LIMIT_PULSE (400)

/* 棰勭暀鍙傛暟锛氳繍鍔ㄥ畬鎴愬悗鐨勯澶栦繚鎶ゆ椂闂达紙褰撳墠鏈娇鐢級 */
#define TASK_STEPPER_MOVE_DONE_MARGIN_MS (5U)

/* 鏂瑰悜鍙嶇浉寮€鍏筹細0=涓嶅弽鐩革紝1=鍙嶇浉 */
#define TASK_STEPPER_X_DIR_INVERT (0U)
#define TASK_STEPPER_Y_DIR_INVERT (0U)

/* ========================= 鐘舵€佺粨鏋勪綋 ========================= */

/**
 * @brief Stepper 浠诲姟鍙鐘舵€佸揩鐓? */
typedef struct
{
    bool configured;               /* 浠诲姟璧勬簮鏄惁鍑嗗瀹屾垚 */
    bool k230_bound;               /* K230 鏄惁宸茬粦瀹?*/
    bool vofa_bound;               /* VOFA 鏄惁宸茬粦瀹?*/
    bool x_axis_bound;             /* X 杞撮┍鍔ㄩ€氶亾鏄惁宸茬粦瀹?*/
    bool y_axis_bound;             /* Y 杞撮┍鍔ㄩ€氶亾鏄惁宸茬粦瀹?*/

    uint8_t dcc_mode;              /* 褰撳墠闀滃儚 DCC 妯″紡锛?/1/2锛?*/
    uint8_t dcc_run_state;         /* 褰撳墠闀滃儚 DCC 杩愯鎬?*/

    uint8_t motor1_id;             /* 鏈€杩戜竴甯?K230 鐨?motor1_id */
    int16_t err1;                  /* 鏈€杩戜竴甯?K230 鐨?err1 */
    uint8_t motor2_id;             /* 鏈€杩戜竴甯?K230 鐨?motor2_id */
    int16_t err2;                  /* 鏈€杩戜竴甯?K230 鐨?err2 */

    int32_t x_pos_pulse;           /* X 杞翠换鍔″眰浼扮畻鑴夊啿浣嶇疆 */
    int32_t y_pos_pulse;           /* Y 杞翠换鍔″眰浼扮畻鑴夊啿浣嶇疆 */
    uint8_t x_busy;                /* 棰勭暀瀛楁 */
    uint8_t y_busy;                /* 棰勭暀瀛楁 */

    uint32_t x_last_pulse_cmd;     /* X 杞存渶杩戜竴娆＄瓑鏁堣剦鍐插懡浠?*/
    uint32_t y_last_pulse_cmd;     /* Y 杞存渶杩戜竴娆＄瓑鏁堣剦鍐插懡浠?*/
    uint16_t x_last_speed_cmd;     /* X 杞存渶杩戜竴娆￠€熷害鍛戒护锛坮pm锛?*/
    uint16_t y_last_speed_cmd;     /* Y 杞存渶杩戜竴娆￠€熷害鍛戒护锛坮pm锛?*/

    uint32_t frame_update_count;   /* 鑾峰彇鍒版柊甯ф鏁?*/
    uint32_t x_cmd_ok_count;       /* X 杞村懡浠ゅ彂閫佹垚鍔熻鏁?*/
    uint32_t y_cmd_ok_count;       /* Y 杞村懡浠ゅ彂閫佹垚鍔熻鏁?*/
    uint32_t x_cmd_drop_count;     /* X 杞村懡浠ゅけ璐?涓㈠純璁℃暟 */
    uint32_t y_cmd_drop_count;     /* Y 杞村懡浠ゅけ璐?涓㈠純璁℃暟 */
    uint32_t vofa_tx_ok_count;     /* VOFA 鍙戦€佹垚鍔熻鏁?*/
    uint32_t vofa_tx_drop_count;   /* VOFA 鍙戦€佸け璐ヨ鏁?*/
} task_stepper_state_t;

/* ========================= 浠诲姟鎺ュ彛 ========================= */

bool task_stepper_prepare_channels(void);
bool task_stepper_is_ready(void);
void StartStepperTask(void *argument);
const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id);

/**
 * @brief 澶栭儴璋冪敤閫熷害鍛戒护鎺ュ彛锛堝唴閮ㄤ細闄愬箙鍒?TASK_STEPPER_VEL_MAX_RPM锛? */
bool task_stepper_send_velocity(uint8_t logic_id,
                                mod_stepper_dir_e dir,
                                uint16_t vel_rpm,
                                uint8_t acc,
                                bool sync_flag);

/**
 * @brief 澶栭儴璋冪敤浣嶇疆鍛戒护鎺ュ彛锛堝唴閮ㄤ細闄愬箙鍒?TASK_STEPPER_POS_MAX_RPM锛? */
bool task_stepper_send_position(uint8_t logic_id,
                                mod_stepper_dir_e dir,
                                uint16_t vel_rpm,
                                uint8_t acc,
                                uint32_t pulse,
                                bool absolute_mode,
                                bool sync_flag);

#endif /* FINAL_GRADUATE_WORK_TASK_STEPPER_H */
