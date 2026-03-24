/**
 * @file    pid_multi.c
 * @brief   多环 PID 组合实现。
 * @details
 * 1. 文件作用：实现多路 PID 组合控制与调参辅助逻辑。
 * 2. 上下层绑定：上层由任务层控制流程调用；下层依赖 PID 基础算法模块。
 */
#include "pid_multi.h"
#include <stddef.h>

/**
 * @brief 默认参数区（可在 pid_config.h 新增同名宏进行覆盖）
 */
#ifdef MOTOR_POSITION_KP
#define PID_MULTI_OUTER_KP_DEFAULT ((float)MOTOR_POSITION_KP)
#else
#define PID_MULTI_OUTER_KP_DEFAULT (1.0f)
#endif

#ifdef MOTOR_POSITION_KI
#define PID_MULTI_OUTER_KI_DEFAULT ((float)MOTOR_POSITION_KI)
#else
#define PID_MULTI_OUTER_KI_DEFAULT (0.0f)
#endif

#ifdef MOTOR_POSITION_KD
#define PID_MULTI_OUTER_KD_DEFAULT ((float)MOTOR_POSITION_KD)
#else
#define PID_MULTI_OUTER_KD_DEFAULT (0.0f)
#endif

#define PID_MULTI_INNER_KP_DEFAULT ((float)MOTOR_SPEED_KP)
#define PID_MULTI_INNER_KI_DEFAULT ((float)MOTOR_SPEED_KI)
#define PID_MULTI_INNER_KD_DEFAULT ((float)MOTOR_SPEED_KD)

#ifdef MOTOR_POSITION_OUTPUT_MAX
#define PID_MULTI_OUTER_OUT_MAX_DEFAULT ((float)MOTOR_POSITION_OUTPUT_MAX)
#else
#define PID_MULTI_OUTER_OUT_MAX_DEFAULT (500.0f)
#endif

#ifdef MOTOR_POSITION_INTEGRAL_MAX
#define PID_MULTI_OUTER_INT_MAX_DEFAULT ((float)MOTOR_POSITION_INTEGRAL_MAX)
#else
#define PID_MULTI_OUTER_INT_MAX_DEFAULT (200.0f)
#endif

#ifdef MOTOR_SPEED_OUTPUT_MAX
#define PID_MULTI_INNER_OUT_MAX_DEFAULT ((float)MOTOR_SPEED_OUTPUT_MAX)
#else
#define PID_MULTI_INNER_OUT_MAX_DEFAULT (1000.0f)
#endif

#ifdef MOTOR_SPEED_INTEGRAL_MAX
#define PID_MULTI_INNER_INT_MAX_DEFAULT ((float)MOTOR_SPEED_INTEGRAL_MAX)
#else
#define PID_MULTI_INNER_INT_MAX_DEFAULT (400.0f)
#endif

#ifdef MOTOR_INNER_TARGET_MAX
#define PID_MULTI_INNER_TARGET_MAX_DEFAULT ((float)MOTOR_INNER_TARGET_MAX)
#else
#define PID_MULTI_INNER_TARGET_MAX_DEFAULT ((float)MOTOR_TARGET_SPEED)
#endif

// 浮点限幅工具函数：把输入约束到 [min, max] 区间
/**
 * @brief 对浮点值执行区间限幅。
 * @param value 待限幅值。
 * @param min 下限值。
 * @param max 上限值。
 * @return 限幅后的值。
 */
static float clamp_float(float value, float min, float max)
{
    float result = value; // 1. 默认输出原值

    // 2. 下限保护
    if (result < min)
        result = min;

    // 3. 上限保护
    if (result > max)
        result = max;

    return result; // 4. 统一出口返回结果
}

// 根据算法类型运行一个 PID 环（位置式或增量式）
/**
 * @brief 根据算法类型运行单个 PID 环并返回输出。
 * @param algo 算法类型（位置式或增量式）。
 * @param pos 位置式 PID 对象。
 * @param inc 增量式 PID 对象。
 * @param target 目标值。
 * @param measure 测量值。
 * @return 当前环输出。
 */
static float run_single_loop(pid_algo_e algo, pid_pos_t *pos, pid_inc_t *inc, float target, float measure)
{
    float output = 0.0f; // 1. 默认输出

    // 2. 按算法类型分发执行
    if (algo == PID_ALGO_POSITION)
    {
        PID_Pos_SetTarget(pos, target);
        output = PID_Pos_Compute(pos, measure);
    }
    else
    {
        PID_Inc_SetTarget(inc, target);
        output = PID_Inc_Compute(inc, measure);
    }

    return output; // 3. 返回当前环输出
}

/**
 * @brief 初始化多环 PID 对象及默认参数。
 * @param multi 多环 PID 对象指针。
 * @return 无。
 */
void PID_Multi_Init(pid_multi_t *multi)
{
    // 1. 空指针保护
    if (multi != NULL)
    {
        // 2. 默认启用串级，且采用“外环位置 + 内环增量”组合
        multi->cascade_enable = 1U;
        multi->outer_algo = PID_ALGO_POSITION;
        multi->inner_algo = PID_ALGO_INCREMENT;

        // 3. 初始化内环目标限幅（防止外环给出过激速度目标）
        multi->inner_target_max = PID_MULTI_INNER_TARGET_MAX_DEFAULT;
        multi->inner_target_min = -PID_MULTI_INNER_TARGET_MAX_DEFAULT;

        // 4. 初始化外环（位置式 + 增量式都初始化，便于后续随时切换算法）
        PID_Pos_Init(&multi->outer_pos,
                     PID_MULTI_OUTER_KP_DEFAULT,
                     PID_MULTI_OUTER_KI_DEFAULT,
                     PID_MULTI_OUTER_KD_DEFAULT,
                     PID_MULTI_OUTER_OUT_MAX_DEFAULT,
                     PID_MULTI_OUTER_INT_MAX_DEFAULT);
        PID_Inc_Init(&multi->outer_inc,
                     PID_MULTI_OUTER_KP_DEFAULT,
                     PID_MULTI_OUTER_KI_DEFAULT,
                     PID_MULTI_OUTER_KD_DEFAULT,
                     PID_MULTI_OUTER_OUT_MAX_DEFAULT);

        // 5. 初始化内环（位置式 + 增量式都初始化，便于后续随时切换算法）
        PID_Pos_Init(&multi->inner_pos,
                     PID_MULTI_INNER_KP_DEFAULT,
                     PID_MULTI_INNER_KI_DEFAULT,
                     PID_MULTI_INNER_KD_DEFAULT,
                     PID_MULTI_INNER_OUT_MAX_DEFAULT,
                     PID_MULTI_INNER_INT_MAX_DEFAULT);
        PID_Inc_Init(&multi->inner_inc,
                     PID_MULTI_INNER_KP_DEFAULT,
                     PID_MULTI_INNER_KI_DEFAULT,
                     PID_MULTI_INNER_KD_DEFAULT,
                     PID_MULTI_INNER_OUT_MAX_DEFAULT);

        // 6. 清空动态状态并设置默认总目标
        PID_Multi_Reset(multi);
        multi->target = (float)MOTOR_TARGET_SPEED;
    }
}

/**
 * @brief 复位多环 PID 运行时状态。
 * @param multi 多环 PID 对象指针。
 * @return 无。
 */
void PID_Multi_Reset(pid_multi_t *multi)
{
    // 1. 空指针保护
    if (multi != NULL)
    {
        // 2. 清空多环对象的动态状态
        multi->target = 0.0f;
        multi->outer_feedback = 0.0f;
        multi->inner_feedback = 0.0f;
        multi->inner_target = 0.0f;
        multi->output = 0.0f;

        // 3. 同时复位外环与内环的全部 PID 运行状态
        PID_Pos_Reset(&multi->outer_pos);
        PID_Inc_Reset(&multi->outer_inc);
        PID_Pos_Reset(&multi->inner_pos);
        PID_Inc_Reset(&multi->inner_inc);
    }
}

/**
 * @brief 设置是否启用串级模式。
 * @param multi 多环 PID 对象指针。
 * @param enable 非零启用串级，零表示单环直控。
 * @return 无。
 */
void PID_Multi_SetCascadeEnable(pid_multi_t *multi, uint8_t enable)
{
    // 1. 空指针保护
    if (multi != NULL)
        // 2. 统一归一化为 0 或 1
        multi->cascade_enable = (enable != 0U) ? 1U : 0U;
}

/**
 * @brief 设置外环算法类型。
 * @param multi 多环 PID 对象指针。
 * @param algo 外环算法类型。
 * @return 无。
 */
void PID_Multi_SetOuterAlgo(pid_multi_t *multi, pid_algo_e algo)
{
    // 1. 空指针保护
    if (multi != NULL)
        // 2. 仅接受合法算法类型
        if ((algo == PID_ALGO_POSITION) || (algo == PID_ALGO_INCREMENT))
            multi->outer_algo = algo;
}

/**
 * @brief 设置内环算法类型。
 * @param multi 多环 PID 对象指针。
 * @param algo 内环算法类型。
 * @return 无。
 */
void PID_Multi_SetInnerAlgo(pid_multi_t *multi, pid_algo_e algo)
{
    // 1. 空指针保护
    if (multi != NULL)
        // 2. 仅接受合法算法类型
        if ((algo == PID_ALGO_POSITION) || (algo == PID_ALGO_INCREMENT))
            multi->inner_algo = algo;
}

/**
 * @brief 设置多环控制总目标值。
 * @param multi 多环 PID 对象指针。
 * @param target 总目标值。
 * @return 无。
 */
void PID_Multi_SetTarget(pid_multi_t *multi, float target)
{
    // 1. 空指针保护
    if (multi != NULL)
        multi->target = target;
}

/**
 * @brief 设置内环目标限幅区间。
 * @param multi 多环 PID 对象指针。
 * @param target_min 内环目标下限。
 * @param target_max 内环目标上限。
 * @return 无。
 */
void PID_Multi_SetInnerTargetLimit(pid_multi_t *multi, float target_min, float target_max)
{
    float temp; // 临时计算变量
    // 1. 空指针保护
    if (multi != NULL)
    {
        // 2. 保证 [min, max] 次序合法
        if (target_min > target_max)
        {
            temp = target_min;
            target_min = target_max;
            target_max = temp;
        }

        // 3. 写入新限幅并立刻裁剪当前内环目标
        multi->inner_target_min = target_min;
        multi->inner_target_max = target_max;
        multi->inner_target = clamp_float(multi->inner_target, multi->inner_target_min, multi->inner_target_max);
    }
}

/**
 * @brief 执行一次多环 PID 计算，返回最终内环输出。
 * @param multi 多环 PID 对象指针。
 * @param outer_feedback 外环反馈值。
 * @param inner_feedback 内环反馈值。
 * @return 最终控制输出。
 */
float PID_Multi_Compute(pid_multi_t *multi, float outer_feedback, float inner_feedback)
{
    float outer_output = 0.0f; // 外环输出（串级时作为内环目标）
    float final_output = 0.0f; // 最终输出（给执行器）

    // 1. 空指针保护
    if (multi != NULL)
    {
        // 2. 记录反馈量，便于调试观察
        multi->outer_feedback = outer_feedback;
        multi->inner_feedback = inner_feedback;

        // 3. 串级模式：先跑外环，再跑内环
        if (multi->cascade_enable != 0U)
        {
            // 3.1 外环根据总目标与外环反馈计算“内环目标”
            outer_output = run_single_loop(multi->outer_algo,
                                           &multi->outer_pos,
                                           &multi->outer_inc,
                                           multi->target,
                                           multi->outer_feedback);

            // 3.2 外环输出做限幅，保护内环目标
            multi->inner_target = clamp_float(outer_output, multi->inner_target_min, multi->inner_target_max);
        }
        else
        {
            // 4. 非串级模式：总目标直接作为内环目标
            multi->inner_target = clamp_float(multi->target, multi->inner_target_min, multi->inner_target_max);
        }

        // 5. 内环根据 inner_target 与 inner_feedback 计算最终控制输出
        final_output = run_single_loop(multi->inner_algo,
                                       &multi->inner_pos,
                                       &multi->inner_inc,
                                       multi->inner_target,
                                       multi->inner_feedback);

        // 6. 存档最终输出并返回
        multi->output = final_output;
    }

    return final_output; // 7. 返回有效输出
}

