/**
 ******************************************************************************
 * @file    pid_multi.h
 * @brief   多环（串级）PID 通用封装接口定义
 * @details
 * 支持外环与内环独立选择控制算法：
 *   1. 位置式 PID
 *   2. 增量式 PID
 *
 * 典型用途：
 *   1. 外环位置 + 内环速度
 *   2. 外环角度 + 内环角速度
 *   3. 关闭串级，仅保留单环（直接用内环）
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_PID_MULTI_H
#define FINAL_GRADUATE_WORK_PID_MULTI_H

#include <stdint.h>
#include "pid_config.h"
#include "pid_pos.h"
#include "pid_inc.h"

/**
 * @brief PID 算法类型枚举
 */
typedef enum
{
    PID_ALGO_POSITION = 0, // 位置式 PID
    PID_ALGO_INCREMENT      // 增量式 PID
} pid_algo_e;

/**
 * @brief 多环 PID 对象结构体
 */
typedef struct
{
    pid_algo_e outer_algo; // 外环算法选择
    pid_algo_e inner_algo; // 内环算法选择
    uint8_t cascade_enable; // 串级使能：1=外环->内环；0=单环直控内环

    float target; // 系统总目标（外部输入）
    float outer_feedback; // 外环反馈量（如位置）
    float inner_feedback; // 内环反馈量（如速度）

    float inner_target; // 由外环计算得到的内环目标
    float inner_target_max; // 内环目标上限
    float inner_target_min; // 内环目标下限

    float output; // 最终控制输出（通常给执行器）

    pid_pos_t outer_pos; // 外环位置式 PID 对象
    pid_inc_t outer_inc; // 外环增量式 PID 对象
    pid_pos_t inner_pos; // 内环位置式 PID 对象
    pid_inc_t inner_inc; // 内环增量式 PID 对象
} pid_multi_t;

/**
 * @brief 初始化多环 PID（可修改参数的通用入口）
 * @details
 * 默认策略：
 *   1. 开启串级
 *   2. 外环算法 = 位置式，内环算法 = 增量式
 *   3. 默认参数来自 pid_config.h（若无专用宏则使用保守默认值）
 * @param multi 多环 PID 对象指针
 */
void PID_Multi_Init(pid_multi_t *multi);

/**
 * @brief 复位多环 PID 的运行状态（不改动参数与限幅）
 * @param multi 多环 PID 对象指针
 */
void PID_Multi_Reset(pid_multi_t *multi);

/**
 * @brief 设置串级使能状态
 * @param multi 多环 PID 对象指针
 * @param enable 1=开启串级，0=关闭串级
 */
void PID_Multi_SetCascadeEnable(pid_multi_t *multi, uint8_t enable);

/**
 * @brief 设置外环算法类型
 * @param multi 多环 PID 对象指针
 * @param algo 外环算法类型
 */
void PID_Multi_SetOuterAlgo(pid_multi_t *multi, pid_algo_e algo);

/**
 * @brief 设置内环算法类型
 * @param multi 多环 PID 对象指针
 * @param algo 内环算法类型
 */
void PID_Multi_SetInnerAlgo(pid_multi_t *multi, pid_algo_e algo);

/**
 * @brief 设置系统总目标值
 * @param multi 多环 PID 对象指针
 * @param target 总目标值
 */
void PID_Multi_SetTarget(pid_multi_t *multi, float target);

/**
 * @brief 设置内环目标限幅（外环输出经该限幅后再进入内环）
 * @param multi 多环 PID 对象指针
 * @param target_min 内环目标下限
 * @param target_max 内环目标上限
 */
void PID_Multi_SetInnerTargetLimit(pid_multi_t *multi, float target_min, float target_max);

/**
 * @brief 执行一次多环 PID 计算
 * @details
 * 当串级开启时：
 *   1. 外环：target - outer_feedback -> outer_output（即 inner_target）
 *   2. 内环：inner_target - inner_feedback -> output
 *
 * 当串级关闭时：
 *   1. 直接使用 target 作为 inner_target，跳过外环
 *
 * @param multi 多环 PID 对象指针
 * @param outer_feedback 外环反馈值
 * @param inner_feedback 内环反馈值
 * @return float 最终控制输出
 */
float PID_Multi_Compute(pid_multi_t *multi, float outer_feedback, float inner_feedback);

#endif /* FINAL_GRADUATE_WORK_PID_MULTI_H */
