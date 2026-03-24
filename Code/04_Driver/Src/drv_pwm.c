/**
 * @file    drv_pwm.c
 * @brief   PWM 驱动层接口实现。
 * @details
 * 1. 文件作用：实现 PWM 通道参数校验、启停控制和占空比写入。
 * 2. 解耦边界：仅处理通道输出，不涉及方向控制与业务调速算法。
 * 3. 上层绑定：`mod_motor` 调用该接口构建功率输出链路。
 * 4. 下层依赖：HAL_TIM_PWM_* 与 CCR 寄存器写入宏。
 */

#include "drv_pwm.h"

/**
 * @brief 校验 PWM 通道号是否合法。
 * @param channel PWM 通道宏值。
 * @return true 通道为 TIM_CHANNEL_1~TIM_CHANNEL_4。
 * @return false 通道不受支持。
 */
static bool is_valid_channel(uint32_t channel)
{
    bool result = false; // 通道合法性校验结果

    // 1. 校验传入的通道宏是否属于 TIM_CHANNEL_1~4。
    if ((channel == TIM_CHANNEL_1) ||
        (channel == TIM_CHANNEL_2) ||
        (channel == TIM_CHANNEL_3) ||
        (channel == TIM_CHANNEL_4))
    {
        result = true;
    }

    // 2. 返回校验结果。
    return result;
}

/**
 * @brief 初始化 PWM 设备对象。
 * @param dev PWM 设备对象指针。
 * @param htim 定时器句柄。
 * @param channel PWM 通道号。
 * @param duty_max 最大占空比。
 * @param invert 占空比反相标志。
 * @return true 初始化成功。
 * @return false 参数非法。
 */
bool drv_pwm_device_init(drv_pwm_dev_t *dev,
                         TIM_HandleTypeDef *htim,
                         uint32_t channel,
                         uint16_t duty_max,
                         bool invert)
{
    bool result = false; // 初始化执行结果，默认失败

    // 1. 参数校验：对象、句柄、占空比上限和通道号必须有效。
    if ((dev != NULL) && (htim != NULL) && (duty_max != 0U) && is_valid_channel(channel))
    {
        // 2. 绑定硬件句柄并保存静态配置。
        dev->htim = htim;
        dev->channel = channel;
        dev->duty_max = duty_max;
        dev->invert = invert;

        // 3. 初始化运行状态为未启动。
        dev->started = false;

        // 4. 返回初始化成功。
        result = true;
    }

    return result;
}

/**
 * @brief 启动 PWM 输出。
 * @param dev PWM 设备对象指针。
 * @return true 启动成功。
 * @return false 参数无效或底层启动失败。
 */
bool drv_pwm_start(drv_pwm_dev_t *dev)
{
    bool result = false; // 启动执行结果，默认失败

    // 1. 参数校验：设备对象和定时器句柄必须有效。
    if ((dev != NULL) && (dev->htim != NULL))
    {
        // 2. 启动前清零比较寄存器，避免瞬时异常输出。
        __HAL_TIM_SET_COMPARE(dev->htim, dev->channel, 0U);

        // 3. 调用 HAL 接口启动对应通道的 PWM。
        if (HAL_TIM_PWM_Start(dev->htim, dev->channel) == HAL_OK)
        {
            dev->started = true;
            result = true;
        }
        else
        {
            // 4. 启动失败时强制保持未启动状态。
            dev->started = false;
        }
    }

    return result;
}

/**
 * @brief 停止 PWM 输出。
 * @param dev PWM 设备对象指针。
 * @return 无返回值。
 */
void drv_pwm_stop(drv_pwm_dev_t *dev)
{
    // 1. 参数校验后停止 PWM 输出，避免空指针访问。
    if ((dev != NULL) && (dev->htim != NULL))
    {
        (void)HAL_TIM_PWM_Stop(dev->htim, dev->channel);

        // 2. 停止后清零比较寄存器，保证引脚处于安全状态。
        __HAL_TIM_SET_COMPARE(dev->htim, dev->channel, 0U);

        // 3. 同步更新软件状态为未启动。
        dev->started = false;
    }
}

/**
 * @brief 设置 PWM 占空比。
 * @param dev PWM 设备对象指针。
 * @param duty 目标占空比值。
 * @return 无返回值。
 */
void drv_pwm_set_duty(drv_pwm_dev_t *dev, uint16_t duty)
{
    uint16_t duty_clamped; // 限幅并处理反相后的最终占空比

    // 1. 参数校验：设备对象和定时器句柄必须有效。
    if ((dev != NULL) && (dev->htim != NULL))
    {
        // 2. 先按上限限幅，防止写入超过配置范围的比较值。
        duty_clamped = (duty > dev->duty_max) ? dev->duty_max : duty;

        // 3. 若启用反相，占空比按 (duty_max - duty) 换算。
        if (dev->invert)
        {
            duty_clamped = (uint16_t)(dev->duty_max - duty_clamped);
        }

        // 4. 把最终占空比写入比较寄存器。
        __HAL_TIM_SET_COMPARE(dev->htim, dev->channel, duty_clamped);
    }
}

/**
 * @brief 获取设备最大占空比配置值。
 * @param dev PWM 设备对象指针。
 * @return uint16_t 最大占空比；参数无效时返回 0。
 */
uint16_t drv_pwm_get_duty_max(const drv_pwm_dev_t *dev)
{
    uint16_t result = 0U; // 最大占空比返回值，默认 0

    // 1. 参数有效时返回设备初始化时配置的最大占空比。
    if (dev != NULL)
    {
        result = dev->duty_max;
    }

    // 2. 返回查询结果。
    return result;
}
