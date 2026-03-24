/**
 * @file    drv_pwm.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   PWM 驱动层接口定义。
 * @details
 * 1. 文件作用：封装 TIM PWM 通道的启停、占空比设置和上限查询能力。
 * 2. 解耦边界：驱动层只处理占空比和通道控制，不承担电机方向或闭环策略。
 * 3. 上层绑定：`mod_motor` 基于该接口构建电机执行通道。
 * 4. 下层依赖：调用 HAL TIM PWM 接口，定时器初始化由 Core 完成。
 * 5. 生命周期：需先关联有效 TIM 句柄和通道，再执行 start/set_duty。
 */
#ifndef FINAL_GRADUATE_WORK_DRV_PWM_H
#define FINAL_GRADUATE_WORK_DRV_PWM_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief PWM 设备对象。
 * @details
 * 保存一个 PWM 通道运行所需的硬件句柄和状态参数。
 */
typedef struct
{
    TIM_HandleTypeDef *htim;   // 定时器句柄
    uint32_t channel;          // TIM 通道号（如 `TIM_CHANNEL_1`）
    uint16_t duty_max;         // 最大占空比参考值
    bool invert;               // 是否反相输出占空比
    bool started;              // PWM 启动状态
} drv_pwm_dev_t;

/**
 * @brief 初始化 PWM 设备对象。
 * @details
 * 完成参数校验并绑定硬件资源，不会直接启动 PWM 输出。
 * @param dev PWM 设备对象指针。
 * @param htim 定时器句柄。
 * @param channel PWM 通道号。
 * @param duty_max 最大占空比值（必须大于 0）。
 * @param invert 占空比反相标志。
 * @return true 初始化成功。
 * @return false 参数非法。
 */
bool drv_pwm_device_init(drv_pwm_dev_t *dev,
                         TIM_HandleTypeDef *htim,
                         uint32_t channel,
                         uint16_t duty_max,
                         bool invert);

/**
 * @brief 启动 PWM 输出。
 * @details
 * 启动前会先将占空比清零，避免启动瞬间异常输出。
 * @param dev PWM 设备对象指针。
 * @return true 启动成功。
 * @return false 参数无效或底层 HAL 启动失败。
 */
bool drv_pwm_start(drv_pwm_dev_t *dev);

/**
 * @brief 停止 PWM 输出。
 * @details
 * 停止后会将比较寄存器清零，保证输出进入安全状态。
 * @param dev PWM 设备对象指针。
 * @return 无返回值。
 */
void drv_pwm_stop(drv_pwm_dev_t *dev);

/**
 * @brief 设置 PWM 占空比。
 * @details
 * 若输入占空比超过 `duty_max` 会自动限幅；若启用反相，会执行反向换算。
 * @param dev PWM 设备对象指针。
 * @param duty 目标占空比值。
 * @return 无返回值。
 */
void drv_pwm_set_duty(drv_pwm_dev_t *dev, uint16_t duty);

/**
 * @brief 获取设备最大占空比配置值。
 * @param dev PWM 设备对象指针。
 * @return uint16_t 最大占空比；参数无效时返回 0。
 */
uint16_t drv_pwm_get_duty_max(const drv_pwm_dev_t *dev);

#endif /* FINAL_GRADUATE_WORK_DRV_PWM_H */
