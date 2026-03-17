#include "drv_encoder.h"

/**
 * @brief 校验编码器计数器位宽参数是否合法。
 * @param counter_bits 计数器位宽配置值。
 * @return true 参数为 16 位或 32 位。
 * @return false 参数非法。
 */
static bool is_valid_counter_bits(uint8_t counter_bits)
{
    /* 变量说明：
     * result: 参数合法性校验结果。
     */
    bool result = false;

    /* 仅允许 16 位或 32 位编码器计数器。 */
    if ((counter_bits == DRV_ENCODER_BITS_16) || (counter_bits == DRV_ENCODER_BITS_32))
    {
        result = true;
    }

    return result;
}

/**
 * @brief 初始化编码器设备对象。
 * @details
 * 该函数用于完成软件对象与硬件定时器之间的绑定，
 * 并设置计数位宽、方向因子及初始状态。
 *
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
    /* 变量说明：
     * result: 初始化执行结果。
     */
    bool result = false;

    /* 先完成指针和位宽参数校验。 */
    if ((dev != NULL) && (htim != NULL) && is_valid_counter_bits(counter_bits))
    {
        /* 绑定硬件句柄并写入设备静态配置。 */
        dev->htim = htim;
        dev->counter_bits = counter_bits;
        dev->direction = invert ? -1 : 1;
        dev->started = false;

        result = true;
    }

    return result;
}

/**
 * @brief 启动编码器计数。
 * @details
 * 启动成功后会将计数器清零，确保后续读取增量时基准一致。
 *
 * @param dev 编码器设备对象指针。
 * @return true 启动成功。
 * @return false 启动失败或参数无效。
 */
bool drv_encoder_start(drv_encoder_dev_t *dev)
{
    /* 变量说明：
     * result: 启动流程结果。
     */
    bool result = false;

    /* 参数检查。 */
    if ((dev != NULL) && (dev->htim != NULL))
    {
        /* 启动 TIM 编码器模式。 */
        if (HAL_TIM_Encoder_Start(dev->htim, TIM_CHANNEL_ALL) == HAL_OK)
        {
            /* 启动后立即清零计数器，避免历史残留值影响本次统计。 */
            __HAL_TIM_SET_COUNTER(dev->htim, 0U);
            dev->started = true;
            result = true;
        }
        else
        {
            /* HAL 启动失败时同步刷新软件状态。 */
            dev->started = false;
        }
    }

    return result;
}

/**
 * @brief 停止编码器计数。
 * @param dev 编码器设备对象指针。
 */
void drv_encoder_stop(drv_encoder_dev_t *dev)
{
    /* 参数检查后停止硬件计数，并清除软件启动标志。 */
    if ((dev != NULL) && (dev->htim != NULL))
    {
        (void)HAL_TIM_Encoder_Stop(dev->htim, TIM_CHANNEL_ALL);
        dev->started = false;
    }
}

/**
 * @brief 复位编码器计数寄存器为 0。
 * @param dev 编码器设备对象指针。
 */
void drv_encoder_reset(drv_encoder_dev_t *dev)
{
    /* 只复位计数器，不修改其他运行参数。 */
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
 *
 * @param dev 编码器设备对象指针。
 * @return int32_t 带符号的脉冲增量值。
 */
int32_t drv_encoder_get_delta(drv_encoder_dev_t *dev)
{
    /* 变量说明：
     * delta_raw: 从硬件计数器读取到的原始增量值。
     * delta_signed: 叠加方向因子后的最终增量值。
     * result: 函数返回值，默认 0。
     */
    int32_t delta_raw;
    int32_t delta_signed;
    int32_t result = 0;

    /* 参数校验。 */
    if ((dev != NULL) && (dev->htim != NULL))
    {
        /* 16 位计数器需先按 int16_t 解释，再扩展到 int32_t。 */
        if (dev->counter_bits == DRV_ENCODER_BITS_16)
            delta_raw = (int32_t)((int16_t)__HAL_TIM_GET_COUNTER(dev->htim));
        else
        {
            /* 32 位计数器可直接读取。 */
            delta_raw = (int32_t)__HAL_TIM_GET_COUNTER(dev->htim);
        }

        /* 读取后清零，下一次调用得到的是新的增量窗口。 */
        __HAL_TIM_SET_COUNTER(dev->htim, 0U);

        /* 应用方向因子：正向为 1，反向为 -1。 */
        delta_signed = delta_raw * (int32_t)dev->direction;
        result = delta_signed;
    }

    return result;
}
