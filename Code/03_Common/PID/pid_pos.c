#include "pid_pos.h"
#include <stddef.h>

// 浮点限幅工具函数：将 value 约束到 [min, max] 区间
static float clamp_float(float value, float min, float max)
{
    float result = value; // 1. 默认输出为原始输入值

    // 2. 下限保护
    if (result < min)
        result = min;

    // 3. 上限保护
    if (result > max)
        result = max;

    return result; // 4. 统一出口返回限幅结果
}

void PID_Pos_Init(pid_pos_t *pid, float kp, float ki, float kd, float out_max, float integral_max)
{
    // 1. 空指针保护
    if (pid != NULL)
    {
        // 2. 若上限传入负值，转成对称正上限
        if (out_max < 0.0f)
            out_max = -out_max;

        // 3. 若积分上限传入负值，转成对称正上限
        if (integral_max < 0.0f)
            integral_max = -integral_max;

        // 4. 写入 PID 核心参数
        pid->kp = kp;
        pid->ki = ki;
        pid->kd = kd;

        // 5. 设置默认对称限幅
        pid->out_max = out_max;
        pid->out_min = -out_max;
        pid->integral_max = integral_max;
        pid->integral_min = -integral_max;

        // 6. 清空运行状态，避免脏数据影响首次输出
        PID_Pos_Reset(pid);
    }
}

void PID_Pos_InitByConfig(pid_pos_t *pid)
{
    // 1. 用 pid_config.h 中的参数初始化 PID
    PID_Pos_Init(pid,
                 (float)MOTOR_SPEED_KP,
                 (float)MOTOR_SPEED_KI,
                 (float)MOTOR_SPEED_KD,
                 PID_POS_OUTPUT_MAX_DEFAULT,
                 PID_POS_INTEGRAL_MAX_DEFAULT);

    // 2. 设置默认目标值
    PID_Pos_SetTarget(pid, (float)MOTOR_TARGET_SPEED);
}

void PID_Pos_SetTarget(pid_pos_t *pid, float target)
{
    // 1. 空指针保护
    if (pid != NULL)
        pid->target = target;
}

void PID_Pos_SetOutputLimit(pid_pos_t *pid, float out_min, float out_max)
{
    float temp;
    // 1. 空指针保护
    if (pid != NULL)
    {
        // 2. 保证 [min, max] 次序合法，若反了就交换
        if (out_min > out_max)
        {
            temp = out_min;
            out_min = out_max;
            out_max = temp;
        }

        // 3. 生效新限幅并立即裁剪当前输出，防止越界残留
        pid->out_min = out_min;
        pid->out_max = out_max;
        pid->output = clamp_float(pid->output, pid->out_min, pid->out_max);
    }
}

void PID_Pos_SetIntegralLimit(pid_pos_t *pid, float integral_min, float integral_max)
{
    float temp;
    // 1. 空指针保护
    if (pid != NULL)
    {
        // 2. 保证 [min, max] 次序合法，若反了就交换
        if (integral_min > integral_max)
        {
            temp = integral_min;
            integral_min = integral_max;
            integral_max = temp;
        }

        // 3. 生效新积分限幅并立即裁剪当前积分量，防止越界残留
        pid->integral_min = integral_min;
        pid->integral_max = integral_max;
        pid->integral = clamp_float(pid->integral, pid->integral_min, pid->integral_max);
    }
}

float PID_Pos_Compute(pid_pos_t *pid, float measure)
{
    float output = 0.0f; // 1. 默认返回 0，确保异常情况下行为可控

    // 2. 空指针保护
    if (pid != NULL)
    {
        // 3. 更新测量值并计算当前误差 e(k)
        pid->measure = measure;
        pid->error = pid->target - pid->measure;

        // 4. 更新积分项并限幅，抑制积分饱和
        pid->integral += pid->error;
        pid->integral = clamp_float(pid->integral, pid->integral_min, pid->integral_max);

        // 5. 计算三项分量，便于调参与在线观察
        pid->p_term = pid->kp * pid->error;
        pid->i_term = pid->ki * pid->integral;
        pid->d_term = pid->kd * (pid->error - pid->last_error);

        // 6. 合成原始输出并执行输出限幅
        output = pid->p_term + pid->i_term + pid->d_term;
        output = clamp_float(output, pid->out_min, pid->out_max);

        // 7. 更新对象状态，准备下个控制周期
        pid->output = output;
        pid->last_error = pid->error;
    }

    return output; // 8. 返回限幅后的有效输出
}

void PID_Pos_Reset(pid_pos_t *pid)
{
    // 1. 空指针保护
    if (pid != NULL)
    {
        // 2. 清空输入/误差/积分/输出状态
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
}
