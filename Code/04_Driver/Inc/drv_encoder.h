/**
 * @file    drv_encoder.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   正交编码器驱动层接口定义。
 * @details
 * 1. 文件作用：封装定时器编码器模式设备的初始化、启停、清零和计数读取。
 * 2. 解耦边界：驱动层仅维护计数读取与方向处理，不承担速度闭环和底盘控制逻辑。
 * 3. 上层绑定：`mod_motor` 基于编码器计数推导速度/位置并参与控制环。
 * 4. 下层依赖：使用 TIM 编码器 HAL 接口访问硬件计数器。
 * 5. 生命周期：设备对象由上层持有，需先 init/start 后再调用读取接口。
 */
#ifndef FINAL_GRADUATE_WORK_DRV_ENCODER_H
#define FINAL_GRADUATE_WORK_DRV_ENCODER_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/** 16 位定时器计数宽度 */
#define DRV_ENCODER_BITS_16 (16U)
/** 32 位定时器计数宽度 */
#define DRV_ENCODER_BITS_32 (32U)

/**
 * @brief 编码器设备对象。
 * @details
 * 该结构体保存编码器驱动运行所需的硬件句柄和状态参数。
 */
typedef struct
{
    TIM_HandleTypeDef *htim;    // 定时器句柄
    uint8_t counter_bits;       // 计数器位宽（16 或 32）
    int8_t direction;           // 方向系数（1 或 -1）
    bool started;               // 启动状态标志
} drv_encoder_dev_t;

/**
 * @brief 初始化编码器设备对象。
 * @details
 * 完成编码器对象与底层定时器句柄绑定，并配置计数位宽与方向参数。
 * @param dev 编码器设备对象指针。
 * @param htim 已配置为编码器模式的定时器句柄。
 * @param counter_bits 计数器位宽，取值为 `DRV_ENCODER_BITS_16` 或 `DRV_ENCODER_BITS_32`。
 * @param invert 方向反转标志，`true` 表示最终增量取反。
 * @return true 初始化成功。
 * @return false 参数无效或位宽配置非法。
 */
bool drv_encoder_device_init(drv_encoder_dev_t *dev,
                             TIM_HandleTypeDef *htim,
                             uint8_t counter_bits,
                             bool invert);

/**
 * @brief 启动编码器计数。
 * @details
 * 启动后会清零计数器，确保后续读取的增量基于新基准点。
 * @param dev 编码器设备对象指针。
 * @return true 启动成功。
 * @return false 参数无效或底层 HAL 启动失败。
 */
bool drv_encoder_start(drv_encoder_dev_t *dev);

/**
 * @brief 停止编码器计数。
 * @param dev 编码器设备对象指针。
 * @return 无返回值。
 */
void drv_encoder_stop(drv_encoder_dev_t *dev);

/**
 * @brief 清零编码器计数器。
 * @param dev 编码器设备对象指针。
 * @return 无返回值。
 */
void drv_encoder_reset(drv_encoder_dev_t *dev);

/**
 * @brief 获取自上次读取以来的编码器增量。
 * @details
 * 读取当前计数值后会自动清零计数器，便于下一周期继续统计增量。
 * @param dev 编码器设备对象指针。
 * @return int32_t 带符号增量值（正负方向由方向系数决定）。
 */
int32_t drv_encoder_get_delta(drv_encoder_dev_t *dev);

#endif /* FINAL_GRADUATE_WORK_DRV_ENCODER_H */
