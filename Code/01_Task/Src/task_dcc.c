#include "task_dcc.h"
#include "task_init.h"
#include "mod_vofa.h"

/**
 * @brief 浮点限幅函数。
 *
 * @details
 * 将输入值限制在 [min, max] 区间，避免 PID 输出越界后导致电机占空比失控。
 */
static float clamp_float(float value, float min, float max)
{
    float result = value;

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
 * @brief 将浮点占空比转换为电机接口使用的 int16 命令值。
 *
 * @details
 * 处理流程：
 * 1. 先按 MOD_MOTOR_DUTY_MAX 做安全限幅。
 * 2. 再按“对称四舍五入”转换成整数，保证正负误差行为一致。
 */
static int16_t convert_to_duty_cmd(float duty_f)
{
    float duty_limited;

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
 * @brief 底盘控制任务入口。
 *
 * @details
 * 控制结构为：
 * - 外环：位置差 PID（根据左右轮位置差输出修正量）。
 * - 内环：左右速度增量 PID（输出占空比）。
 *
 * 模式切换：
 * - MODE 0：平衡/同步控制（已实现）。
 * - MODE 1：循迹预留（暂未实现）。
 */
void StartDccTask(void *argument)
{
    pid_pos_t pos_pid;
    pid_inc_t left_speed_pid;
    pid_inc_t right_speed_pid;
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx();
    uint8_t control_mode = TASK_DCC_MODE_BALANCE;

    (void)argument;

    /* 先等待 InitTask 完成硬件绑定与模块初始化，再启动本任务业务控制。 */
    task_wait_init_done();

    /* 初始化外环位置 PID。 */
    PID_Pos_Init(&pos_pid,
                 MOTOR_POS_KP,
                 MOTOR_POS_KI,
                 MOTOR_POS_KD,
                 MOTOR_POS_OUTPUT_MAX,
                 MOTOR_POS_INTEGRAL_MAX);
    PID_Pos_SetTarget(&pos_pid, (float)MOTOR_TARGET_ERROR);

    /* 初始化左右两个内环速度 PID。 */
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

    /* 预留启动等待时间，给底层驱动和机械系统稳定窗口。 */
    osDelay(3000U);

    for (;;)
    {
        /* 非阻塞读取模式切换信号（由 KEY3 点击触发）。 */
        if (osSemaphoreAcquire(Sem_TaskChangeHandle, 0U) == osOK)
        {
            /* 0/1 翻转。 */
            control_mode ^= 1U;

            /* 切模式时重置 PID 内部状态，避免积分残留造成跃变。 */
            PID_Pos_Reset(&pos_pid);
            PID_Inc_Reset(&left_speed_pid);
            PID_Inc_Reset(&right_speed_pid);
            PID_Pos_SetTarget(&pos_pid, (float)MOTOR_TARGET_ERROR);

            /* 切换瞬间先将电机输出清零，避免冲击。 */
            mod_motor_set_duty(MOD_MOTOR_LEFT, 0);
            mod_motor_set_duty(MOD_MOTOR_RIGHT, 0);
        }

        /* 统一刷新编码器采样，更新速度和累计位置。 */
        mod_motor_tick();

        if (control_mode == TASK_DCC_MODE_BALANCE)
        {
            int64_t left_pos;
            int64_t right_pos;
            float pos_error;
            float outer_output;
            float left_target_speed;
            float right_target_speed;
            float left_feedback_speed;
            float right_feedback_speed;
            float left_duty_f;
            float right_duty_f;
            float vofa_payload[4];

            /* 左右轮累计位置差作为外环误差。 */
            left_pos = mod_motor_get_position(MOD_MOTOR_LEFT);
            right_pos = mod_motor_get_position(MOD_MOTOR_RIGHT);
            pos_error = (float)(left_pos - right_pos);

            /* 外环输出修正量，并再次限幅到允许范围。 */
            outer_output = PID_Pos_Compute(&pos_pid, -pos_error);
            outer_output = clamp_float(outer_output, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

            /* 根据外环修正量拆分左右目标速度。 */
            left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - outer_output);
            right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + outer_output);

            /* 读取实际速度反馈。 */
            left_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_LEFT);
            right_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_RIGHT);

            /* 内环计算占空比。 */
            PID_Inc_SetTarget(&left_speed_pid, left_target_speed);
            PID_Inc_SetTarget(&right_speed_pid, right_target_speed);
            left_duty_f = PID_Inc_Compute(&left_speed_pid, left_feedback_speed);
            right_duty_f = PID_Inc_Compute(&right_speed_pid, right_feedback_speed);

            /* 下发电机输出命令。 */
            mod_motor_set_duty(MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
            mod_motor_set_duty(MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));

            /* 若 VOFA 已绑定，则按固定通道顺序上报调试数据。 */
            if (mod_vofa_is_bound(p_vofa_ctx))
            {
                vofa_payload[0] = (float)MOTOR_TARGET_SPEED; /* CH0：全局目标速度 */
                vofa_payload[1] = left_target_speed;         /* CH1：左轮目标速度 */
                vofa_payload[2] = right_target_speed;        /* CH2：右轮目标速度 */
                vofa_payload[3] = pos_error;                 /* CH3：位置差 */

                (void)mod_vofa_send_float_ctx(p_vofa_ctx, "DccCtrl", vofa_payload, 4U);
            }
        }
        else
        {
            /* MODE 1：循迹控制预留。 */
        }

        /* 固定 20ms 调度周期（50Hz）。 */
        osDelay(TASK_DCC_PERIOD_MS);
    }
}
