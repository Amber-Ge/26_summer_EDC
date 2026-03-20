/**
 ******************************************************************************
 * @file    task_stepper.h
 * @brief   Stepper任务层接口（当前版本用于K230数据观测与VOFA转发）
 *
 * @details
 * 当前任务层的职责边界如下：
 * 1. 不下发任何步进电机控制命令，仅做数据观测链路验证。
 * 2. 以固定20ms周期，从K230协议模块读取“当前缓冲区可解析到的最新一帧”。
 * 3. 将缓存的4个观测量通过VOFA发出：id1, err1, id2, err2。
 * 4. 保留mod_stepper头文件与预留接口，后续恢复电机控制时无需改调用关系。
 *
 * 典型数据流：
 * K230(UART4 RX DMA) -> mod_k230环形缓冲区 -> task_stepper取最新帧 -> VOFA(UART3 TX DMA)
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_STEPPER_H
#define FINAL_GRADUATE_WORK_TASK_STEPPER_H

#include "cmsis_os.h"
#include "mod_k230.h"
#include "mod_stepper.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 调度参数 ========================= */

/**
 * @brief StepperTask主循环周期（毫秒）
 *
 * 当前需求为每20ms执行一次：
 * 1. 拉取K230最新帧
 * 2. 发送VOFA观测数据
 */
#define TASK_STEPPER_PERIOD_MS      (20U)

/**
 * @brief Stepper任务启动开关
 * - 1：任务正常运行
 * - 0：任务启动后立即挂起（用于临时屏蔽任务影响）
 */
#define TASK_STEPPER_STARTUP_ENABLE (1U)

/**
 * @brief VOFA发送标签
 *
 * 最终发送格式由mod_vofa决定，标签用于区分数据来源。
 */
#define TASK_STEPPER_VOFA_TAG       ("K230")

/* ========================= 状态结构 ========================= */

/**
 * @brief Stepper任务观测状态结构
 *
 * 该结构体用于：
 * 1. 保存最近一次有效K230数据
 * 2. 暴露链路状态（K230是否绑定、VOFA是否绑定）
 * 3. 提供基础统计（帧更新次数、VOFA发送成功/丢弃次数）
 */
typedef struct
{
    bool configured;            // 任务准备流程是否执行完成
    bool k230_bound;            // 本周期检查到的K230绑定状态
    bool vofa_bound;            // 本周期检查到的VOFA绑定状态

    uint8_t motor1_id;          // 最近一帧中的电机1 ID
    int16_t err1;               // 最近一帧中的电机1误差
    uint8_t motor2_id;          // 最近一帧中的电机2 ID
    int16_t err2;               // 最近一帧中的电机2误差

    uint32_t frame_update_count; // 成功获取新有效帧的累计次数
    uint32_t vofa_tx_ok_count;   // VOFA发送成功累计次数（DMA启动成功）
    uint32_t vofa_tx_drop_count; // VOFA发送失败累计次数（未绑定或忙等原因）
} task_stepper_state_t;

/* ========================= 任务层接口 ========================= */

/**
 * @brief Stepper任务准备函数
 *
 * 当前版本行为：
 * 1. 清零观测状态
 * 2. 标记任务已准备完成
 *
 * @return true 始终返回成功
 */
bool task_stepper_prepare_channels(void);

/**
 * @brief 查询Stepper任务是否已完成准备
 * @return true 已准备
 * @return false 未准备
 */
bool task_stepper_is_ready(void);

/**
 * @brief Stepper任务入口
 * @param argument RTOS任务参数（当前未使用）
 */
void StartStepperTask(void *argument);

/**
 * @brief 获取Stepper任务状态只读指针
 * @param logic_id 预留参数，当前版本不参与索引
 * @return 状态指针；若任务未准备则返回NULL
 */
const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id);

/**
 * @brief 预留接口：发送速度命令
 *
 * 当前版本不调用mod_stepper，固定返回false。
 */
bool task_stepper_send_velocity(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag);

/**
 * @brief 预留接口：发送位置命令
 *
 * 当前版本不调用mod_stepper，固定返回false。
 */
bool task_stepper_send_position(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag);

#endif /* FINAL_GRADUATE_WORK_TASK_STEPPER_H */
