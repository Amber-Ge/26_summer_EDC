/**
 ******************************************************************************
 * @file    mod_motor.h
 * @brief   双电机模块接口定义
 * @details
 * 封装电机驱动（PWM + H 桥）与编码器反馈，提供速度/位置相关接口。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_MOTOR_H
#define FINAL_GRADUATE_WORK_MOD_MOTOR_H // 头文件防重复包含宏

#include "drv_pwm.h"
#include "drv_gpio.h"
#include "drv_encoder.h"
#include <stdint.h>
#include <stdbool.h>
#include "tim.h"

/** 电机占空比限幅上限 */
#define MOD_MOTOR_DUTY_MAX (1000U) // 电机占空比限幅上限

/**
 * @brief 电机逻辑 ID。
 */
typedef enum
{
    MOD_MOTOR_LEFT = 0,   // 左轮
    MOD_MOTOR_RIGHT,      // 右轮
    MOD_MOTOR_MAX         // 电机通道数量上限
} mod_motor_id_e;

/**
 * @brief 电机运行模式。
 */
typedef enum
{
    MOTOR_MODE_DRIVE = 0, // 正常驱动模式
    MOTOR_MODE_BRAKE,     // 刹车模式
    MOTOR_MODE_COAST      // 滑行模式
} mod_motor_mode_e;

/**
 * @brief 初始化电机模块。
 * @details
 * 完成底层 PWM/编码器初始化，并将电机状态复位到安全默认模式。
 */
void mod_motor_init(void);

/**
 * @brief 设置电机运行模式。
 * @param id 电机逻辑 ID。
 * @param mode 目标模式。
 */
void mod_motor_set_mode(mod_motor_id_e id, mod_motor_mode_e mode);

/**
 * @brief 设置电机输出占空比。
 * @details
 * 正值表示正转，负值表示反转，内部会执行限幅与过零保护。
 *
 * @param id 电机逻辑 ID。
 * @param duty 目标占空比（范围通常为 `-MOD_MOTOR_DUTY_MAX ~ MOD_MOTOR_DUTY_MAX`）。
 */
void mod_motor_set_duty(mod_motor_id_e id, int16_t duty);

/**
 * @brief 电机状态周期更新函数。
 * @details
 * 建议在固定周期任务中调用，用于更新速度、位置并处理挂起指令。
 */
void mod_motor_tick(void);

/**
 * @brief 获取当前速度估计值。
 * @param id 电机逻辑 ID。
 * @return int32_t 当前速度。
 */
int32_t mod_motor_get_speed(mod_motor_id_e id);

/**
 * @brief 获取累计位置。
 * @param id 电机逻辑 ID。
 * @return int64_t 累计位置计数。
 */
int64_t mod_motor_get_position(mod_motor_id_e id);

#endif /* FINAL_GRADUATE_WORK_MOD_MOTOR_H */
