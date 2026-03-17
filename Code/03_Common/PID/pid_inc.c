#include "pid_inc.h"

void PID_Inc_Init(pid_inc_t *pid, float kp, float ki, float kd, float out_max)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_max = out_max;
    pid->out_min = -out_max; // 默认为对称限幅，如需不对称可在外部手动修改 out_min
    
    PID_Inc_Reset(pid);
}

void PID_Inc_SetTarget(pid_inc_t *pid, float target)
{
    pid->target = target;
}

float PID_Inc_Compute(pid_inc_t *pid, float measure)
{
    float delta_out;
    
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

void PID_Inc_Reset(pid_inc_t *pid)
{
    pid->target = 0.0f;
    pid->measure = 0.0f;
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->prev_error = 0.0f;
    pid->output = 0.0f;
}