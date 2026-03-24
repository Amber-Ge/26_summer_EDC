/**
 * @file    pid_pos.h
 * @brief   位置式 PID 控制器接口定义。
 * @details
 * 1. 文件作用：声明位置式 PID 数据结构、参数配置和计算接口。
 * 2. 解耦边界：仅提供控制算法，不绑定具体硬件执行器与采样通道。
 * 3. 上层绑定：控制任务可用于位置/角度等慢变量闭环。
 * 4. 内置能力：积分限幅与输出限幅，降低积分饱和和执行器过驱风险。
 */
#ifndef FINAL_GRADUATE_WORK_PID_POS_H
#define FINAL_GRADUATE_WORK_PID_POS_H

#include <stdint.h>
#include "pid_config.h"

/**
 * @brief 位置式 PID 默认输出上限（若未在 pid_config.h 中配置）
 */
#ifndef PID_POS_OUTPUT_MAX_DEFAULT
#define PID_POS_OUTPUT_MAX_DEFAULT (1000.0f)
#endif

/**
 * @brief 位置式 PID 默认积分上限（若未在 pid_config.h 中配置）
 */
#ifndef PID_POS_INTEGRAL_MAX_DEFAULT
#define PID_POS_INTEGRAL_MAX_DEFAULT (500.0f)
#endif

/**
 * @brief 位置式 PID 对象结构体
 */
typedef struct
{
    float kp; // 比例系数
    float ki; // 积分系数
    float kd; // 微分系数

    float target; // 目标值
    float measure; // 当前测量值
    float error; // 当前误差 e(k)
    float last_error; // 上一拍误差 e(k-1)

    float integral; // 误差积分 Σe

    float p_term; // 比例分量（便于调试观察）
    float i_term; // 积分分量（便于调试观察）
    float d_term; // 微分分量（便于调试观察）

    float out_max; // 输出上限
    float out_min; // 输出下限
    float integral_max; // 积分上限
    float integral_min; // 积分下限

    float output; // 当前控制输出
} pid_pos_t;

/**
 * @brief 初始化位置式 PID 参数与状态
 * @param pid PID 对象指针
 * @param kp 比例系数
 * @param ki 积分系数
 * @param kd 微分系数
 * @param out_max 输出上限（下限默认设置为 -out_max）
 * @param integral_max 积分上限（下限默认设置为 -integral_max）
 */
void PID_Pos_Init(pid_pos_t *pid, float kp, float ki, float kd, float out_max, float integral_max);

/**
 * @brief 使用 pid_config.h 中参数初始化位置式 PID
 * @details
 * 默认映射：
 *   1. Kp/Ki/Kd 使用 MOTOR_SPEED_KP/KI/KD
 *   2. 目标值使用 MOTOR_TARGET_SPEED
 *   3. 输出与积分上限使用本文件默认宏
 *
 * 若工程需要专用位置环参数，可在 pid_config.h 中新增宏并改写本函数。
 * @param pid PID 对象指针
 */
void PID_Pos_InitByConfig(pid_pos_t *pid);

/**
 * @brief 设置 PID 目标值
 * @param pid PID 对象指针
 * @param target 目标设定值
 */
void PID_Pos_SetTarget(pid_pos_t *pid, float target);

/**
 * @brief 设置 PID 输出上下限
 * @param pid PID 对象指针
 * @param out_min 输出下限
 * @param out_max 输出上限
 */
void PID_Pos_SetOutputLimit(pid_pos_t *pid, float out_min, float out_max);

/**
 * @brief 设置 PID 积分上下限
 * @param pid PID 对象指针
 * @param integral_min 积分下限
 * @param integral_max 积分上限
 */
void PID_Pos_SetIntegralLimit(pid_pos_t *pid, float integral_min, float integral_max);

/**
 * @brief 执行一次位置式 PID 计算
 * @param pid PID 对象指针
 * @param measure 当前测量值
 * @return float 限幅后的控制输出
 */
float PID_Pos_Compute(pid_pos_t *pid, float measure);

/**
 * @brief 复位 PID 内部误差、积分与输出状态
 * @param pid PID 对象指针
 */
void PID_Pos_Reset(pid_pos_t *pid);

#endif /* FINAL_GRADUATE_WORK_PID_POS_H */
