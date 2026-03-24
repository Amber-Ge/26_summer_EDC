/**
 * @file    pid_inc.c
 * @brief   增量式 PID 实现。
 * @details
 * 1. 文件作用：实现增量式 PID 初始化与迭代计算逻辑。
 * 2. 上下层绑定：上层由运动控制流程调用；下层不依赖硬件接口。
 */
#include "pid_inc.h"

/**
 * @brief 初始化增量式 PID 对象参数并清空内部状态。
 * @param pid PID 对象指针。
 * @param kp 比例系数。
 * @param ki 积分系数。
 * @param kd 微分系数。
 * @param out_max 输出上限（下限默认为 `-out_max`）。
 * @return 无。
 */
void PID_Inc_Init(pid_inc_t *pid, float kp, float ki, float kd, float out_max)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_max = out_max;
    pid->out_min = -out_max; // 默认为对称限幅，如需不对称可在外部手动修改 out_min
    
    PID_Inc_Reset(pid);
}

/**
 * @brief 设置增量式 PID 目标值。
 * @param pid PID 对象指针。
 * @param target 目标值。
 * @return 无。
 */
void PID_Inc_SetTarget(pid_inc_t *pid, float target)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    pid->target = target;
}

/**
 * @brief 执行一次增量式 PID 迭代计算并更新输出。
 * @param pid PID 对象指针。
 * @param measure 当前测量值。
 * @return 本次迭代后的输出值（已限幅）。
 */
float PID_Inc_Compute(pid_inc_t *pid, float measure)
{
    float delta_out; // 本周期输出增量
    
    pid->measure = measure;
    
    // 1. 计算当前误差
    pid->error = pid->target - pid->measure;

    // 2. 增量式 PID 公式推导:
    // delta_u = Kp * (e(k) - e(k-1)) 
    //         + Ki * e(k) 
    //         + Kd * (e(k) - 2*e(k-1) + e(k-2))
    delta_out = pid->kp * (pid->error - pid->last_error) +
                pid->ki * pid->error +
                pid->kd * (pid->error - 2.0f * pid->last_error + pid->prev_error);

    // 3. 累加增量到当前输出
    pid->output += delta_out;

    // 4. 输出限幅 (核心安全保护)
    if (pid->output > pid->out_max) pid->output = pid->out_max;
    if (pid->output < pid->out_min) pid->output = pid->out_min;

    // 5. 更新历史状态变量
    pid->prev_error = pid->last_error;
    pid->last_error = pid->error;

    return pid->output;
}

/**
 * @brief 复位增量式 PID 内部状态量。
 * @param pid PID 对象指针。
 * @return 无。
 */
void PID_Inc_Reset(pid_inc_t *pid)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    pid->target = 0.0f;
    pid->measure = 0.0f;
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->prev_error = 0.0f;
    pid->output = 0.0f;
}
