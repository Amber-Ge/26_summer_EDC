/**
 * @file    pid_inc.c
 * @brief   增量式 PID 实现。
 * @details
 * 1. 文件作用：实现增量式 PID 初始化、目标更新、迭代计算与状态复位。
 * 2. 上下层绑定：上层由任务层按固定周期调用；下层不依赖硬件接口。
 */
#include "pid_inc.h"
#include <stddef.h>

/**
 * @brief 初始化增量式 PID 参数并复位内部状态。
 * @param pid PID 对象指针。
 * @param kp 比例系数。
 * @param ki 积分系数。
 * @param kd 微分系数。
 * @param out_max 输出绝对值上限（下限自动对称设置）。
 */
void PID_Inc_Init(pid_inc_t *pid, float kp, float ki, float kd, float out_max)
{
    // 步骤1：空指针保护，避免非法访问。
    if (pid == NULL)
    {
        return;
    }

    // 步骤2：若输出上限为负值，转换为正值，保持对称限幅语义。
    if (out_max < 0.0f)
    {
        out_max = -out_max;
    }

    // 步骤3：写入 PID 参数与限幅边界。
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_max = out_max;
    pid->out_min = -out_max;

    // 步骤4：清零运行时状态，避免历史脏值影响首次计算。
    PID_Inc_Reset(pid);
}

/**
 * @brief 设置增量式 PID 目标值。
 * @param pid PID 对象指针。
 * @param target 目标值。
 */
void PID_Inc_SetTarget(pid_inc_t *pid, float target)
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
 * @brief 执行一次增量式 PID 计算并返回限幅输出。
 * @param pid PID 对象指针。
 * @param measure 当前测量值。
 * @return float 本周期输出值（空指针时返回 0）。
 */
float PID_Inc_Compute(pid_inc_t *pid, float measure)
{
    float delta_out = 0.0f; // 输出增量：delta_u(k)。
    float output = 0.0f; // 临时输出值：用于统一出口返回。

    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return 0.0f;
    }

    // 步骤2：更新测量值并计算当前误差 e(k)。
    pid->measure = measure;
    pid->error = pid->target - pid->measure;

    // 步骤3：依据增量式 PID 公式计算本周期输出增量。
    // delta_u = Kp*(e(k)-e(k-1)) + Ki*e(k) + Kd*(e(k)-2e(k-1)+e(k-2))
    delta_out = pid->kp * (pid->error - pid->last_error) +
                pid->ki * pid->error +
                pid->kd * (pid->error - (2.0f * pid->last_error) + pid->prev_error);

    // 步骤4：累加增量得到当前输出。
    pid->output += delta_out;

    // 步骤5：输出限幅，保证执行器输入在安全区间。
    if (pid->output > pid->out_max)
    {
        pid->output = pid->out_max;
    }
    if (pid->output < pid->out_min)
    {
        pid->output = pid->out_min;
    }

    // 步骤6：更新历史误差，为下一周期微分/增量计算提供基准。
    pid->prev_error = pid->last_error;
    pid->last_error = pid->error;

    // 步骤7：统一出口返回当前输出。
    output = pid->output;
    return output;
}

/**
 * @brief 复位增量式 PID 内部状态。
 * @param pid PID 对象指针。
 */
void PID_Inc_Reset(pid_inc_t *pid)
{
    // 步骤1：空指针保护。
    if (pid == NULL)
    {
        return;
    }

    // 步骤2：清空目标、测量、误差与输出状态。
    pid->target = 0.0f;
    pid->measure = 0.0f;
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->prev_error = 0.0f;
    pid->output = 0.0f;
}
