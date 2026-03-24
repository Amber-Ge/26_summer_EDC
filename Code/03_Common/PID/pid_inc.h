/**
 * @file    pid_inc.h
 * @brief   增量式 PID 接口定义。
 * @details
 * 1. 文件作用：声明增量式 PID 数据结构、参数与计算接口。
 * 2. 解耦边界：仅提供算法计算，不依赖具体硬件执行器。
 * 3. 上层绑定：`DccTask` 等控制流程按周期调用，输出增量控制量。
 * 4. 生命周期：调用方负责 init/reset/set_target，并维护调用周期一致性。
 */
#ifndef FINAL_GRADUATE_WORK_PID_INC_H
#define FINAL_GRADUATE_WORK_PID_INC_H // 头文件防重复包含宏

#include <stdint.h>

/**
 * @brief 增量式 PID 对象结构体。
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
    float prev_error; // 上上拍误差 e(k-2)

    float out_max; // 输出上限
    float out_min; // 输出下限
    float output; // 当前控制输出
} pid_inc_t;

/**
 * @brief 初始化增量式 PID 参数与状态。
 * @param pid PID 对象指针。
 * @param kp 比例系数。
 * @param ki 积分系数。
 * @param kd 微分系数。
 * @param out_max 输出上限（下限默认设置为 `-out_max`）。
 */
void PID_Inc_Init(pid_inc_t *pid, float kp, float ki, float kd, float out_max);

/**
 * @brief 设置 PID 目标值。
 * @param pid PID 对象指针。
 * @param target 目标设定值。
 */
void PID_Inc_SetTarget(pid_inc_t *pid, float target);

/**
 * @brief 执行一次增量式 PID 计算。
 * @param pid PID 对象指针。
 * @param measure 当前测量值。
 * @return float 限幅后的控制输出值。
 */
float PID_Inc_Compute(pid_inc_t *pid, float measure);

/**
 * @brief 复位 PID 内部误差与输出状态。
 * @param pid PID 对象指针。
 */
void PID_Inc_Reset(pid_inc_t *pid);

#endif /* FINAL_GRADUATE_WORK_PID_INC_H */

