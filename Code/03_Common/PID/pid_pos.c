/**
 * @file    pid_pos.c
 * @brief   位置式 PID 实现。
 * @details
 * 1. 文件作用：实现位置式 PID 初始化、限幅配置、迭代计算与复位逻辑。
 * 2. 上下层绑定：上层由控制任务周期调用；下层不依赖硬件接口。
 */
#include "pid_pos.h"
#include <stddef.h>

/**
 * @brief 对浮点值执行区间限幅。
 * @param value 待限幅值。
 * @param min 下限值。
 * @param max 上限值。
 * @return float 限幅后的值。
 */
static float clamp_float(float value, float min, float max)
{
    float result = value; // result：限幅结果，默认等于输入值。

    // 步骤1：下限保护。
    if (result < min)
    {
        result = min;
    }

    // 步骤2：上限保护。
    if (result > max)
    {
        result = max;
    }

    // 步骤3：返回统一限幅结果。
    return result;
}

/**
 * @brief 初始化位置式 PID 参数与默认限幅。
 * @param pid PID 对象指针。
 * @param kp 比例系数。
 * @param ki 积分系数。
 * @param kd 微分系数。
 * @param out_max 输出绝对值上限。
 * @param integral_max 积分绝对值上限。
 */
void PID_Pos_Init(pid_pos_t *pid, float kp, float ki, float kd, float out_max, float integral_max)
{
    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return;
    }

    // 步骤2：统一输出上限符号，保持对称限幅。
    if (out_max < 0.0f)
    {
        out_max = -out_max;
    }

    // 步骤3：统一积分上限符号，保持对称限幅。
    if (integral_max < 0.0f)
    {
        integral_max = -integral_max;
    }

    // 步骤4：写入 PID 核心参数。
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;

    // 步骤5：设置输出与积分默认对称限幅。
    pid->out_max = out_max;
    pid->out_min = -out_max;
    pid->integral_max = integral_max;
    pid->integral_min = -integral_max;

    // 步骤6：复位运行时状态，避免脏数据进入控制链路。
    PID_Pos_Reset(pid);
}

/**
 * @brief 使用 `pid_config.h` 默认参数初始化位置式 PID。
 * @param pid PID 对象指针。
 */
void PID_Pos_InitByConfig(pid_pos_t *pid)
{
    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return;
    }

    // 步骤2：按配置文件默认参数完成初始化。
    PID_Pos_Init(pid,
                 (float)MOTOR_SPEED_KP,
                 (float)MOTOR_SPEED_KI,
                 (float)MOTOR_SPEED_KD,
                 PID_POS_OUTPUT_MAX_DEFAULT,
                 PID_POS_INTEGRAL_MAX_DEFAULT);

    // 步骤3：写入默认目标值。
    PID_Pos_SetTarget(pid, (float)MOTOR_TARGET_SPEED);
}

/**
 * @brief 设置位置式 PID 目标值。
 * @param pid PID 对象指针。
 * @param target 目标值。
 */
void PID_Pos_SetTarget(pid_pos_t *pid, float target)
{
    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return;
    }

    // 步骤2：更新目标值。
    pid->target = target;
}

/**
 * @brief 设置输出限幅区间，并裁剪当前输出。
 * @param pid PID 对象指针。
 * @param out_min 输出下限。
 * @param out_max 输出上限。
 */
void PID_Pos_SetOutputLimit(pid_pos_t *pid, float out_min, float out_max)
{
    float temp = 0.0f; // temp：交换上下限时使用的临时变量。

    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return;
    }

    // 步骤2：保证上下限顺序合法，必要时交换。
    if (out_min > out_max)
    {
        temp = out_min;
        out_min = out_max;
        out_max = temp;
    }

    // 步骤3：写入新的输出限幅。
    pid->out_min = out_min;
    pid->out_max = out_max;

    // 步骤4：立即裁剪当前输出，防止历史越界残留。
    pid->output = clamp_float(pid->output, pid->out_min, pid->out_max);
}

/**
 * @brief 设置积分限幅区间，并裁剪当前积分。
 * @param pid PID 对象指针。
 * @param integral_min 积分下限。
 * @param integral_max 积分上限。
 */
void PID_Pos_SetIntegralLimit(pid_pos_t *pid, float integral_min, float integral_max)
{
    float temp = 0.0f; // temp：交换上下限时使用的临时变量。

    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return;
    }

    // 步骤2：保证上下限顺序合法，必要时交换。
    if (integral_min > integral_max)
    {
        temp = integral_min;
        integral_min = integral_max;
        integral_max = temp;
    }

    // 步骤3：写入新的积分限幅。
    pid->integral_min = integral_min;
    pid->integral_max = integral_max;

    // 步骤4：立即裁剪当前积分，防止历史越界残留。
    pid->integral = clamp_float(pid->integral, pid->integral_min, pid->integral_max);
}

/**
 * @brief 执行一次位置式 PID 计算。
 * @param pid PID 对象指针。
 * @param measure 当前测量值。
 * @return float 限幅后的输出值。
 */
float PID_Pos_Compute(pid_pos_t *pid, float measure)
{
    float output = 0.0f; // output：当前周期输出，默认 0。

    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return 0.0f;
    }

    // 步骤2：更新测量值并计算误差 e(k)。
    pid->measure = measure;
    pid->error = pid->target - pid->measure;

    // 步骤3：积分累加并执行积分限幅。
    pid->integral += pid->error;
    pid->integral = clamp_float(pid->integral, pid->integral_min, pid->integral_max);

    // 步骤4：计算 P/I/D 三项分量，便于调参与观测。
    pid->p_term = pid->kp * pid->error;
    pid->i_term = pid->ki * pid->integral;
    pid->d_term = pid->kd * (pid->error - pid->last_error);

    // 步骤5：合成输出并执行输出限幅。
    output = pid->p_term + pid->i_term + pid->d_term;
    output = clamp_float(output, pid->out_min, pid->out_max);

    // 步骤6：更新运行状态，供下一周期继续计算。
    pid->output = output;
    pid->last_error = pid->error;

    // 步骤7：返回当前有效输出。
    return output;
}

/**
 * @brief 复位位置式 PID 运行状态。
 * @param pid PID 对象指针。
 */
void PID_Pos_Reset(pid_pos_t *pid)
{
    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return;
    }

    // 步骤2：清空目标、测量、误差、积分与输出状态。
    pid->target = 0.0f;
    pid->measure = 0.0f;
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->integral = 0.0f;
    pid->p_term = 0.0f;
    pid->i_term = 0.0f;
    pid->d_term = 0.0f;
    pid->output = 0.0f;
}
