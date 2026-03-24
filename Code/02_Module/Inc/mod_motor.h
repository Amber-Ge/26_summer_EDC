/**
 * @file    mod_motor.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   双电机模块接口定义。
 * @details
 * 1. 文件作用：封装双电机方向/PWM/编码器的组合控制，提供统一速度与位置读写接口。
 * 2. 解耦边界：本模块负责执行机构抽象，不承载 PID、轨迹规划和模式切换策略。
 * 3. 上层绑定：`DccTask` 等控制任务读取速度/位置并下发占空比目标。
 * 4. 下层依赖：通过 `drv_pwm` 输出占空比、`drv_gpio` 控制方向、`drv_encoder` 采集反馈。
 * 5. 生命周期：先绑定硬件映射并初始化，再周期调用 `tick` 更新反馈量。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_MOTOR_H
#define FINAL_GRADUATE_WORK_MOD_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "drv_encoder.h"
#include "main.h"

/** 电机占空比上限 */
#define MOD_MOTOR_DUTY_MAX (1000U)

/** 电机逻辑通道ID */
typedef enum
{
    MOD_MOTOR_LEFT = 0,   // 左电机通道
    MOD_MOTOR_RIGHT,      // 右电机通道
    MOD_MOTOR_MAX         // 通道总数
} mod_motor_id_e;

/** 电机运行模式 */
typedef enum
{
    MOTOR_MODE_DRIVE = 0, // 驱动模式
    MOTOR_MODE_BRAKE,     // 刹车模式
    MOTOR_MODE_COAST      // 滑行模式
} mod_motor_mode_e;

/** 单个电机通道硬件绑定配置 */
typedef struct
{
    GPIO_TypeDef *in1_port;        // TB6612 IN1 所在 GPIO 端口
    uint16_t in1_pin;              // TB6612 IN1 引脚号
    GPIO_TypeDef *in2_port;        // TB6612 IN2 所在 GPIO 端口
    uint16_t in2_pin;              // TB6612 IN2 引脚号

    TIM_HandleTypeDef *pwm_htim;   // PWM 定时器句柄
    uint32_t pwm_channel;          // PWM 输出通道号
    bool pwm_invert;               // PWM 占空比是否反相

    TIM_HandleTypeDef *enc_htim;   // 编码器定时器句柄
    uint8_t enc_counter_bits;      // 编码器计数器位宽（16/32）
    bool enc_invert;               // 编码器方向是否反向
} mod_motor_hw_cfg_t;

/**
 * @brief 绑定电机硬件映射表。
 * @param map 映射表首地址。
 * @param map_num 映射项数量，必须等于 `MOD_MOTOR_MAX`。
 * @return true 绑定成功。
 * @return false 绑定失败（参数非法或映射项校验失败）。
 */
bool mod_motor_bind_map(const mod_motor_hw_cfg_t *map, uint8_t map_num);

/**
 * @brief 解绑电机硬件映射表。
 * @return 无返回值。
 */
void mod_motor_unbind_map(void);

/**
 * @brief 查询电机模块是否已绑定硬件映射。
 * @return true 已绑定。
 * @return false 未绑定。
 */
bool mod_motor_is_bound(void);

/**
 * @brief 初始化电机模块。
 * @details
 * 会按已绑定映射依次初始化每个通道的 PWM 与编码器，并将输出置为滑行安全态。
 * @return 无返回值。
 */
void mod_motor_init(void);

/**
 * @brief 设置电机运行模式。
 * @param id 电机通道ID。
 * @param mode 目标运行模式。
 * @return 无返回值。
 */
void mod_motor_set_mode(mod_motor_id_e id, mod_motor_mode_e mode);

/**
 * @brief 设置电机占空比指令。
 * @details
 * 仅在 DRIVE 模式且硬件就绪时生效；内部会执行限幅与过零保护逻辑。
 * @param id 电机通道ID。
 * @param duty 目标占空比（支持正反方向）。
 * @return 无返回值。
 */
void mod_motor_set_duty(mod_motor_id_e id, int16_t duty);

/**
 * @brief 电机周期更新入口。
 * @details
 * 周期读取编码器增量并刷新速度/位置，同时处理过零挂起命令。
 * @return 无返回值。
 */
void mod_motor_tick(void);

/**
 * @brief 获取指定电机当前速度。
 * @param id 电机通道ID。
 * @return int32_t 当前速度增量读数；非法参数或未绑定时返回 0。
 */
int32_t mod_motor_get_speed(mod_motor_id_e id);

/**
 * @brief 获取指定电机累计位置。
 * @param id 电机通道ID。
 * @return int64_t 累计位置读数；非法参数或未绑定时返回 0。
 */
int64_t mod_motor_get_position(mod_motor_id_e id);

#endif /* FINAL_GRADUATE_WORK_MOD_MOTOR_H */
