/**
 * @file    pid_config.h
 * @brief   PID 参数配置头文件。
 * @details
 * 1. 文件作用：集中定义控制目标与 PID 默认参数，避免参数散落在任务代码中。
 * 2. 解耦边界：本文件仅提供编译期常量，不包含控制计算与硬件访问逻辑。
 * 3. 上层绑定：`pid_pos`、`pid_inc`、`pid_multi` 以及控制任务统一引用本参数集。
 * 4. 维护原则：参数调整后需同步记录“修改原因 + 影响链路 + 回归结果”。
 */
#ifndef FINAL_GRADUATE_WORK_PID_CONFIG_H
#define FINAL_GRADUATE_WORK_PID_CONFIG_H

/* ========================= 控制目标默认值 ========================= */

/** 底盘默认目标速度。 */
#define MOTOR_TARGET_SPEED (10)
/** 底盘默认目标误差（保留项）。 */
#define MOTOR_TARGET_ERROR (0)

/* ========================= 位置环参数（左右轮差分纠偏） ========================= */

/** 位置环比例系数：根据左右轮累计里程差进行纠偏。 */
#define MOTOR_POS_KP (0.0025f)
/** 位置环积分系数：默认关闭，避免里程积分过冲。 */
#define MOTOR_POS_KI (0.0f)
/** 位置环微分系数：默认关闭，按振荡情况再开启。 */
#define MOTOR_POS_KD (0.0f)
/** 位置环输出限幅：约束左右速度差分比例。 */
#define MOTOR_POS_OUTPUT_MAX (0.5f)
/** 位置环积分限幅：抑制积分饱和。 */
#define MOTOR_POS_INTEGRAL_MAX (5000.0f)

/* ========================= 速度环参数（轮速闭环） ========================= */

/** 速度环比例系数：越大响应越快，过大可能抖动。 */
#define MOTOR_SPEED_KP (8.0f)
/** 速度环积分系数：决定达到目标速度的稳态能力。 */
#define MOTOR_SPEED_KI (3.0f)
/** 速度环微分系数：用于抑制速度波动。 */
#define MOTOR_SPEED_KD (0.2f)

#endif /* FINAL_GRADUATE_WORK_PID_CONFIG_H */
