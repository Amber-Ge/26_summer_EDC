/**
 * @file    pid_pos.c
 * @brief   位置式 PID 实现。
 * @details
 * 1. 文件作用：实现位置式 PID 初始化与控制计算逻辑。
 * 2. 上下层绑定：上层由运动控制流程调用；下层不依赖硬件接口。
 */
#include "pid_pos.h"
#include <stddef.h>

// 浮点限幅工具函数：将 value 约束到 [min, max] 区间
/**
 * @brief 对浮点值执行区间限幅。
 * @param value 待限幅值。
 * @param min 下限值。
 * @param max 上限值。
 * @return 限幅后的值。
 */
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

/**
 * @brief 初始化位置式 PID 参数与限幅配置。
 * @param pid PID 对象指针。
 * @param kp 比例系数。
 * @param ki 积分系数。
 * @param kd 微分系数。
 * @param out_max 输出绝对值上限。
 * @param integral_max 积分绝对值上限。
 * @return 无。
 */
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

/**
 * @brief 使用 `pid_config.h` 默认参数初始化位置式 PID。
 * @param pid PID 对象指针。
 * @return 无。
 */
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

/**
 * @brief 设置位置式 PID 目标值。
 * @param pid PID 对象指针。
 * @param target 目标值。
 * @return 无。
 */
void PID_Pos_SetTarget(pid_pos_t *pid, float target)
{
    // 1. 空指针保护
    if (pid != NULL)
        pid->target = target;
}

/**
 * @brief 设置输出限幅区间，并立即裁剪当前输出。
 * @param pid PID 对象指针。
 * @param out_min 输出下限。
 * @param out_max 输出上限。
 * @return 无。
 */
void PID_Pos_SetOutputLimit(pid_pos_t *pid, float out_min, float out_max)
{
    float temp; // 临时计算变量
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

/**
 * @brief 设置积分限幅区间，并立即裁剪当前积分值。
 * @param pid PID 对象指针。
 * @param integral_min 积分下限。
 * @param integral_max 积分上限。
 * @return 无。
 */
void PID_Pos_SetIntegralLimit(pid_pos_t *pid, float integral_min, float integral_max)
{
    float temp; // 临时计算变量
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

/**
 * @brief 执行一次位置式 PID 计算并更新内部状态。
 * @param pid PID 对象指针。
 * @param measure 当前测量值。
 * @return 本周期输出值（已限幅）。
 */
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

/**
 * @brief 复位位置式 PID 内部状态量。
 * @param pid PID 对象指针。
 * @return 无。
 */
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

