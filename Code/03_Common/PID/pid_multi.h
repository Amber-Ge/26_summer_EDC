/**
 * @file    pid_multi.h
 * @brief   多环（串级）PID 通用封装接口定义。
 * @details
 * 1. 文件作用：声明外环/内环可选算法的串级 PID 对象与计算接口。
 * 2. 解耦边界：仅提供控制算法组合框架，不依赖电机与传感器驱动。
 * 3. 上层绑定：任务层可按场景选择单环或串级模式。
 * 4. 生命周期：调用方负责初始化、参数配置与周期调用。
 */
#ifndef FINAL_GRADUATE_WORK_PID_MULTI_H
#define FINAL_GRADUATE_WORK_PID_MULTI_H

#include <stdint.h>
#include "pid_config.h"
#include "pid_inc.h"
#include "pid_pos.h"

/**
 * @brief PID 算法类型枚举。
 */
typedef enum
{
    PID_ALGO_POSITION = 0, // 位置式 PID。
    PID_ALGO_INCREMENT      // 增量式 PID。
} pid_algo_e;

/**
 * @brief 多环 PID 对象结构体。
 */
typedef struct
{
    pid_algo_e outer_algo; // 外环算法类型。
    pid_algo_e inner_algo; // 内环算法类型。
    uint8_t cascade_enable; // 串级使能：1=外环->内环，0=单环直控。

    float target; // 系统总目标（上层输入）。
    float outer_feedback; // 外环反馈（如位置）。
    float inner_feedback; // 内环反馈（如速度）。

    float inner_target; // 外环输出后的内环目标。
    float inner_target_max; // 内环目标上限。
    float inner_target_min; // 内环目标下限。

    float output; // 最终控制输出。

    pid_pos_t outer_pos; // 外环位置式 PID 对象。
    pid_inc_t outer_inc; // 外环增量式 PID 对象。
    pid_pos_t inner_pos; // 内环位置式 PID 对象。
    pid_inc_t inner_inc; // 内环增量式 PID 对象。
} pid_multi_t;

/**
 * @brief 初始化多环 PID（默认串级模式）。
 * @param multi 多环 PID 对象指针。
 */
void PID_Multi_Init(pid_multi_t *multi);

/**
 * @brief 复位多环 PID 运行状态（不改参数与限幅）。
 * @param multi 多环 PID 对象指针。
 */
void PID_Multi_Reset(pid_multi_t *multi);

/**
 * @brief 设置串级模式开关。
 * @param multi 多环 PID 对象指针。
 * @param enable 1=启用串级，0=关闭串级。
 */
void PID_Multi_SetCascadeEnable(pid_multi_t *multi, uint8_t enable);

/**
 * @brief 设置外环算法。
 * @param multi 多环 PID 对象指针。
 * @param algo 外环算法类型。
 */
void PID_Multi_SetOuterAlgo(pid_multi_t *multi, pid_algo_e algo);

/**
 * @brief 设置内环算法。
 * @param multi 多环 PID 对象指针。
 * @param algo 内环算法类型。
 */
void PID_Multi_SetInnerAlgo(pid_multi_t *multi, pid_algo_e algo);

/**
 * @brief 设置系统总目标值。
 * @param multi 多环 PID 对象指针。
 * @param target 目标值。
 */
void PID_Multi_SetTarget(pid_multi_t *multi, float target);

/**
 * @brief 设置内环目标限幅区间。
 * @param multi 多环 PID 对象指针。
 * @param target_min 内环目标下限。
 * @param target_max 内环目标上限。
 */
void PID_Multi_SetInnerTargetLimit(pid_multi_t *multi, float target_min, float target_max);

/**
 * @brief 执行一次多环 PID 计算。
 * @details
 * 1. 串级开启：外环输出先生成 `inner_target`，再进入内环。
 * 2. 串级关闭：总目标直接作为 `inner_target` 进入内环。
 *
 * @param multi 多环 PID 对象指针。
 * @param outer_feedback 外环反馈值。
 * @param inner_feedback 内环反馈值。
 * @return float 最终输出值。
 */
float PID_Multi_Compute(pid_multi_t *multi, float outer_feedback, float inner_feedback);

#endif /* FINAL_GRADUATE_WORK_PID_MULTI_H */
