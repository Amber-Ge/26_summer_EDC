#include "drv_pwm.h"

static bool is_valid_channel(uint32_t channel)
{
    bool result = false; // 1. 定义返回结果变量，默认为 false

    // 2. 校验传入的通道宏是否为 TIM_CHANNEL_1 到 4 之一
    if ((channel == TIM_CHANNEL_1) ||
        (channel == TIM_CHANNEL_2) ||
        (channel == TIM_CHANNEL_3) ||
        (channel == TIM_CHANNEL_4))
        result = true;

    // 3. 统一返回校验结果
    return result;
}

bool drv_pwm_device_init(drv_pwm_dev_t *dev,
                         TIM_HandleTypeDef *htim,
                         uint32_t channel,
                         uint16_t duty_max,
                         bool invert)
{
    bool result = false; // 1. 定义返回结果变量，默认初始化失败

    // 2. 拦截空指针，并校验占空比上限是否非零以及通道的合法性
    if ((dev != NULL) && (htim != NULL) && (duty_max != 0U) && is_valid_channel(channel))
    {
        // 3. 将传入的定时器句柄绑定到设备结构体上下文中
        dev->htim = htim;
        // 4. 记录定时器通道
        dev->channel = channel;
        // 5. 记录最大占空比，用于后续的限幅和反转计算
        dev->duty_max = duty_max;
        // 6. 记录是否开启占空比逻辑反转
        dev->invert = invert;
        // 7. 初始化 PWM 启动状态标志位为 false
        dev->started = false;

        // 8. 初始化各项参数成功，更新结果为 true
        result = true;
    }

    // 9. 统一返回初始化结果
    return result;
}

bool drv_pwm_start(drv_pwm_dev_t *dev)
{
    bool result = false; // 1. 定义返回结果变量，默认启动失败

    // 2. 检查设备实例和定时器句柄指针是否有效
    if ((dev != NULL) && (dev->htim != NULL))
    {
        // 3. 启动前强制将底层的比较寄存器（占空比）清零，防止启动瞬间输出不可控的高电平
        __HAL_TIM_SET_COMPARE(dev->htim, dev->channel, 0U);

        // 4. 调用 HAL 库函数启动对应通道的 PWM 发生器
        if (HAL_TIM_PWM_Start(dev->htim, dev->channel) == HAL_OK)
        {
            // 5. 底层启动成功，更新启动状态标志位为 true
            dev->started = true;
            result = true; // 6. 更新返回结果为 true
        }
        else
            // 7. 如果底层启动失败，确保状态标志位为 false
            dev->started = false;
    }

    // 8. 统一返回启动结果
    return result;
}

void drv_pwm_stop(drv_pwm_dev_t *dev)
{
    // 1. 检查设备实例和定时器句柄指针是否有效
    if ((dev != NULL) && (dev->htim != NULL))
    {
        // 2. 调用 HAL 库函数停止对应通道的 PWM 发生器
        (void)HAL_TIM_PWM_Stop(dev->htim, dev->channel);

        // 3. 停止后将底层的比较寄存器强制清零，确保引脚恢复安全状态
        __HAL_TIM_SET_COMPARE(dev->htim, dev->channel, 0U);

        // 4. 更新启动状态标志位为 false
        dev->started = false;
    }
    // 5. 统一出口（void函数自然结束）
}

void drv_pwm_set_duty(drv_pwm_dev_t *dev, uint16_t duty)
{
    uint16_t duty_clamped; // 用于暂存经过限幅和反转处理后的最终占空比值

    // 1. 检查设备实例和定时器句柄指针是否有效
    if ((dev != NULL) && (dev->htim != NULL))
    {
        // 2. 占空比限幅保护：如果目标占空比大于最大设定值，则强行截断为最大值
        duty_clamped = (duty > dev->duty_max) ? dev->duty_max : duty;

        // 3. 判断是否需要逻辑反转处理
        if (dev->invert)
            // 4. 如果开启了反转，实际写入的值为 (最大值 - 目标值)
            duty_clamped = (uint16_t)(dev->duty_max - duty_clamped);

        // 5. 使用宏操作直接将计算好的最终占空比写入底层比较寄存器，应用新占空比
        __HAL_TIM_SET_COMPARE(dev->htim, dev->channel, duty_clamped);
    }
}

uint16_t drv_pwm_get_duty_max(const drv_pwm_dev_t *dev)
{
    uint16_t result = 0U; // 1. 定义返回结果变量，默认最大占空比为 0

    // 2. 检查设备实例指针是否有效
    if (dev != NULL)
        // 3. 提取初始化时配置的最大占空比值
        result = dev->duty_max;

    // 4. 统一返回最大占空比
    return result;
}