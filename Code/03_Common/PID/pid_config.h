/**
 * @file    pid_config.h
 * @brief   PID 参数配置头文件。
 * @details
 * 1. 文件作用：集中定义控制目标和 PID 参数默认值，避免散落在任务代码中。
 * 2. 解耦边界：仅提供编译期常量，不包含任何控制计算和硬件访问逻辑。
 * 3. 上层绑定：`pid_pos`/`pid_inc`/`pid_multi` 及控制任务统一引用本参数集。
 * 4. 生命周期：参数在编译期固化，运行期可通过替换宏或外部参数机制扩展。
 */
#ifndef FINAL_GRADUATE_WORK_PID_CONFIG_H
#define FINAL_GRADUATE_WORK_PID_CONFIG_H

/** 目标值参数（基准速度与目标误差） */
#define MOTOR_TARGET_SPEED 10
#define MOTOR_TARGET_ERROR 0

/** 位置环参数（用于左右轮里程差闭环） */
#define MOTOR_POS_KP             0.0025f  // 位置环比例系数：根据左右轮累计里程差进行纠偏
#define MOTOR_POS_KI             0.0f    // 位置环积分系数：先默认关闭，避免里程积分过冲
#define MOTOR_POS_KD             0.0f    // 位置环微分系数：先默认关闭，后续按实际振荡情况再开启
#define MOTOR_POS_OUTPUT_MAX     0.5f    // 位置环输出限幅：你指定为 0.5，用于约束左右速度差分比例
#define MOTOR_POS_INTEGRAL_MAX   5000.0f // 位置环积分限幅：抑制积分饱和

/** 速度环参数（用于轮速闭环） */
#define MOTOR_SPEED_KP      8.0f   // 响应速度：越大响应越快，过大容易产生高频抖动
#define MOTOR_SPEED_KI      3.0f   // 核心动力：增量式的主动力，决定达到目标速度的快慢
#define MOTOR_SPEED_KD      0.2f   // 阻尼作用：抑制速度波动

#endif

