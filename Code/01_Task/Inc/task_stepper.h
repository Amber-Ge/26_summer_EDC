/**
 ******************************************************************************
 * @file    task_stepper.h
 * @brief   步进任务层接口与通道映射配置。
 *
 * @details
 * 分层原则（本文件是关键边界）：
 * 1. 协议层（mod_stepper）只负责“向某个 driver_addr 发命令”。
 * 2. 任务层（task_stepper）负责“逻辑电机ID -> 协议上下文”的映射。
 * 3. InitTask 负责统一绑定（与 VOFA/K230 风格一致），StepperTask 只跑控制循环。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_TASK_STEPPER_H
#define FINAL_GRADUATE_WORK_TASK_STEPPER_H

#include "cmsis_os.h"
#include "mod_k230.h"
#include "mod_stepper.h"
#include "usart.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 调度参数 ========================= */

/**
 * @brief StepperTask 主循环周期（毫秒）。
 *
 * 需求为每 20ms 处理一次 K230 误差并进行位置修正，因此固定为 20。
 */
#define TASK_STEPPER_PERIOD_MS                    (20U)

/**
 * @brief 任务启停开关。
 * - 1：运行控制循环。
 * - 0：启动后挂起（用于临时调试）。
 */
#define TASK_STEPPER_STARTUP_ENABLE               (1U)

/**
 * @brief 是否在 InitTask 绑定阶段发送一次使能命令。
 *
 * 该设置可将“驱动使能”行为收敛到初始化阶段，避免业务循环中混入一次性启动动作。
 */
#define TASK_STEPPER_ENABLE_AT_BIND               (true)

/* ========================= 通道配置 ========================= */

/**
 * @brief 通道开关。
 *
 * 当前需求是双电机，因此 CH1/CH2 都启用。
 */
#define TASK_STEPPER_ENABLE_CH1                   (1U)
#define TASK_STEPPER_ENABLE_CH2                   (1U)

/**
 * @brief CH1 映射参数。
 */
#define TASK_STEPPER_CH1_HUART                    (&huart5)
#define TASK_STEPPER_CH1_LOGIC_ID                 (1U)
#define TASK_STEPPER_CH1_DRIVER_ADDR              (1U)
#define TASK_STEPPER_CH1_MAX_SPEED_RPM            (100U)
#define TASK_STEPPER_CH1_POS_ERR_IS_CW            (true)

/**
 * @brief CH2 映射参数。
 */
#define TASK_STEPPER_CH2_HUART                    (&huart2)
#define TASK_STEPPER_CH2_LOGIC_ID                 (2U)
#define TASK_STEPPER_CH2_DRIVER_ADDR              (2U)
#define TASK_STEPPER_CH2_MAX_SPEED_RPM            (100U)
#define TASK_STEPPER_CH2_POS_ERR_IS_CW            (true)

/* ========================= 误差修正策略参数 ========================= */

/**
 * @brief 误差死区。
 *
 * 当 |error| <= 死区时，本周期不下发位置修正命令。
 */
#define TASK_STEPPER_ERR_DEADBAND                 (0)

/**
 * @brief 误差到脉冲的线性系数。
 *
 * 计算公式：
 * pulse = abs(error) * TASK_STEPPER_PULSE_PER_ERR
 */
#define TASK_STEPPER_PULSE_PER_ERR                (1U)

/**
 * @brief 脉冲输出限幅。
 *
 * - MIN 仅在 pulse 非 0 时生效，避免“有误差但脉冲被截为0”。
 * - MAX 用于限制单周期修正步长，防止过激动作。
 */
#define TASK_STEPPER_PULSE_MIN                    (1U)
#define TASK_STEPPER_PULSE_MAX                    (400U)

/**
 * @brief 位置模式公共参数（修正环默认使用）。
 */
#define TASK_STEPPER_POS_ACC                      (0U)
#define TASK_STEPPER_POS_ABSOLUTE                 (false)
#define TASK_STEPPER_POS_SYNC_FLAG                (false)

/* ========================= 状态结构 ========================= */

/**
 * @brief 单通道可观测状态。
 *
 * 这些字段用于调试与上层观察，不参与协议层逻辑决策。
 */
typedef struct
{
    bool configured;              // 通道配置是否已装载。prepare_channels() 开始处理该通道时置 true，表示该槽位有效。
    bool bound;                   // 协议上下文是否已绑定成功。true 才允许该通道下发速度/位置命令。
    bool enabled;                 // 初始化阶段“使能命令”是否发送成功。仅用于观测初始化结果，不代表驱动实时在线状态。
    uint8_t logic_id;             // 任务层逻辑电机ID。用于匹配 K230 帧中的 motor_id 并路由到对应通道。
    uint8_t driver_addr;          // 协议层驱动地址。真正下发指令时写入协议帧第 1 字节，决定目标驱动器。
    uint16_t max_speed_rpm;       // 本通道速度上限（RPM）。任务层会先按此限幅，再调用协议层发送命令。
    bool positive_err_is_cw;      // 误差符号到方向的映射策略。true=正误差走 CW，false=正误差走 CCW。

    int16_t last_err;             // 最近一次用于控制计算的误差值（来自 K230）。每次消费到新帧后会刷新。
    uint32_t last_pulse_cmd;      // 最近一次实际下发的位置脉冲值。可用于判断当前控制输出是否过大或长期为 0。
    mod_stepper_dir_e last_dir;   // 最近一次下发方向。用于调试“误差正负与电机转向是否一致”问题。

    uint32_t tx_cmd_count;        // 发送成功计数（语义是 DMA 启动成功）。不是“驱动执行完成计数”。
    uint32_t tx_drop_count;       // 发送失败或放弃计数（如未绑定/速度为0/串口忙/启动DMA失败等）用于统计丢命令情况。
} task_stepper_state_t;

/* ========================= 任务层对外接口 ========================= */

/**
 * @brief 在初始化阶段准备全部步进通道（创建/绑定/可选使能）。
 *
 * 建议调用位置：
 * - InitTask 中，在释放 Sem_Init 前调用。
 *
 * @return true 全部通道成功；false 存在任一通道失败。
 */
bool task_stepper_prepare_channels(void);

/**
 * @brief 查询通道准备流程是否已经执行。
 */
bool task_stepper_is_ready(void);

/**
 * @brief StepperTask 任务入口。
 */
void StartStepperTask(void *argument);

/**
 * @brief 按逻辑ID查询通道状态（只读）。
 * @param logic_id 逻辑电机ID。
 * @return 状态指针；未找到返回 NULL。
 */
const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id);

/**
 * @brief 任务层保留的速度模式接口。
 *
 * 说明：
 * - 该接口是“按逻辑ID路由”的包装层。
 * - 最终会转发到 mod_stepper_velocity。
 */
bool task_stepper_send_velocity(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag);

/**
 * @brief 任务层保留的位置模式接口（手动调试/覆盖控制可用）。
 *
 * 说明：
 * - 该接口同样按逻辑ID路由到对应协议上下文。
 */
bool task_stepper_send_position(uint8_t logic_id, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag);

#endif /* FINAL_GRADUATE_WORK_TASK_STEPPER_H */
