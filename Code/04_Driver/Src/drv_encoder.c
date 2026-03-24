/**
 * @file    drv_encoder.c
 * @brief   正交编码器驱动层接口实现。
 * @details
 * 1. 文件作用：实现编码器设备参数校验、启停控制、计数清零与增量读取。
 * 2. 解耦边界：仅负责计数采集与方向处理，不承担速度控制和业务容错策略。
 * 3. 上层绑定：`mod_motor` 周期读取增量并更新速度/位置估计。
 * 4. 下层依赖：依赖 HAL TIM 编码器模式接口访问定时器计数器。
 */

#include "drv_encoder.h"

/**
 * @brief 校验编码器计数器位宽参数是否合法。
 * @param counter_bits 计数器位宽配置值。
 * @return true 参数为 16 位或 32 位。
 * @return false 参数非法。
 */
static bool is_valid_counter_bits(uint8_t counter_bits)
{
    bool result = false; // 位宽参数合法性标志

    // 1. 仅允许 16 位或 32 位两种计数宽度。
    if ((counter_bits == DRV_ENCODER_BITS_16) || (counter_bits == DRV_ENCODER_BITS_32))
    {
        result = true;
    }

    // 2. 返回校验结果。
    return result;
}

/**
 * @brief 初始化编码器设备对象。
 * @details
 * 该函数用于完成软件对象与硬件定时器之间的绑定，
 * 并设置计数位宽、方向因子及初始状态。
 * @param dev 编码器设备对象指针。
 * @param htim 编码器模式定时器句柄。
 * @param counter_bits 计数器位宽（16/32 位）。
 * @param invert 方向反转标志（true 表示方向取反）。
 * @return true 初始化成功。
 * @return false 参数非法或校验失败。
 */
bool drv_encoder_device_init(drv_encoder_dev_t *dev,
                             TIM_HandleTypeDef *htim,
                             uint8_t counter_bits,
                             bool invert)
{
    bool result = false; // 初始化执行结果

    // 1. 参数校验：对象、句柄和位宽配置都必须合法。
    if ((dev != NULL) && (htim != NULL) && is_valid_counter_bits(counter_bits))
    {
        // 2. 绑定硬件句柄并写入设备静态配置。
        dev->htim = htim;
        dev->counter_bits = counter_bits;
        dev->direction = invert ? -1 : 1;
        dev->started = false;

        // 3. 返回初始化成功。
        result = true;
    }

    return result;
}

/**
 * @brief 启动编码器计数。
 * @details
 * 启动成功后会将计数器清零，确保后续读取增量时基准一致。
 * @param dev 编码器设备对象指针。
 * @return true 启动成功。
 * @return false 启动失败或参数无效。
 */
bool drv_encoder_start(drv_encoder_dev_t *dev)
{
    bool result = false; // 启动流程执行结果

    // 1. 参数校验：设备对象及其定时器句柄必须有效。
    if ((dev != NULL) && (dev->htim != NULL))
    {
        // 2. 启动 TIM 编码器模式。
        if (HAL_TIM_Encoder_Start(dev->htim, TIM_CHANNEL_ALL) == HAL_OK)
        {
            // 3. 启动后立即清零计数器，避免历史值影响本轮统计。
            __HAL_TIM_SET_COUNTER(dev->htim, 0U);
            dev->started = true;
            result = true;
        }
        else
        {
            // 4. HAL 启动失败时同步更新软件状态。
            dev->started = false;
        }
    }

    return result;
}

/**
 * @brief 停止编码器计数。
 * @param dev 编码器设备对象指针。
 * @return 无返回值。
 */
void drv_encoder_stop(drv_encoder_dev_t *dev)
{
    // 1. 参数校验通过后停止硬件计数。
    if ((dev != NULL) && (dev->htim != NULL))
    {
        (void)HAL_TIM_Encoder_Stop(dev->htim, TIM_CHANNEL_ALL);

        // 2. 停止后清除软件启动标志，保持状态一致。
        dev->started = false;
    }
}

/**
 * @brief 复位编码器计数寄存器为 0。
 * @param dev 编码器设备对象指针。
 * @return 无返回值。
 */
void drv_encoder_reset(drv_encoder_dev_t *dev)
{
    // 1. 参数校验通过后仅清零计数器，不修改其他配置。
    if ((dev != NULL) && (dev->htim != NULL))
    {
        __HAL_TIM_SET_COUNTER(dev->htim, 0U);
    }
}

/**
 * @brief 获取编码器自上次读取后的增量值。
 * @details
 * 该函数读取当前计数器，按位宽转换为有符号值，
 * 读取后会清零计数器，便于下次获取新的增量。
 * @param dev 编码器设备对象指针。
 * @return int32_t 带符号的脉冲增量值。
 */
int32_t drv_encoder_get_delta(drv_encoder_dev_t *dev)
{
    int32_t delta_raw;      // 原始增量值（来自硬件计数器）
    int32_t delta_signed;   // 应用方向因子后的有符号增量
    int32_t result = 0;     // 函数返回值，默认 0

    // 1. 参数校验：设备对象与句柄必须有效。
    if ((dev != NULL) && (dev->htim != NULL))
    {
        // 2. 按计数器位宽解释当前计数值，避免符号扩展错误。
        if (dev->counter_bits == DRV_ENCODER_BITS_16)
        {
            delta_raw = (int32_t)((int16_t)__HAL_TIM_GET_COUNTER(dev->htim));
        }
        else
        {
            delta_raw = (int32_t)__HAL_TIM_GET_COUNTER(dev->htim);
        }

        // 3. 读取后清零计数器，保证下一次读取的是新的增量窗口。
        __HAL_TIM_SET_COUNTER(dev->htim, 0U);

        // 4. 应用方向因子并输出最终结果。
        delta_signed = delta_raw * (int32_t)dev->direction;
        result = delta_signed;
    }

    return result;
}
