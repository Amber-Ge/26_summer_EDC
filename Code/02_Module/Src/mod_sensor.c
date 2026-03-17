#include "mod_sensor.h"

#include "drv_gpio.h"

#include <string.h>

/** 寻线传感器有效电平定义：低电平表示检测到线 */
#define SENSOR_LINE_LEVEL GPIO_LEVEL_LOW

/* 最近一次计算得到的归一化权重 */
static float s_current_weight = 0.0f;
/* 最近一次采样得到的原始位图 */
static uint16_t s_raw_data = 0U;
/* 当前生效的传感器映射表 */
static mod_sensor_map_item_t s_sensor_map_active[MOD_SENSOR_CHANNEL_NUM];
/* 映射绑定状态标志 */
static bool s_sensor_map_bound = false;

/**
 * @brief 校验单路传感器映射项是否合法
 */
static bool mod_sensor_check_item(const mod_sensor_map_item_t *item)
{
    // 1. 指针为空直接失败。
    if (item == NULL)
    {
        return false;
    }

    // 2. GPIO端口不能为空。
    if (item->port == NULL)
    {
        return false;
    }

    // 3. 通过校验。
    return true;
}

bool mod_sensor_bind_map(const mod_sensor_map_item_t *map, uint8_t map_num)
{
    // 1. 基础参数校验。
    if ((map == NULL) || (map_num != MOD_SENSOR_CHANNEL_NUM))
    {
        return false;
    }

    // 2. 逐项校验映射项。
    for (uint8_t i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++)
    {
        if (!mod_sensor_check_item(&map[i]))
        {
            return false;
        }
    }

    // 3. 保存映射并置位绑定标志。
    memcpy(s_sensor_map_active, map, sizeof(s_sensor_map_active));
    s_sensor_map_bound = true;

    // 4. 返回绑定成功。
    return true;
}

void mod_sensor_unbind_map(void)
{
    // 1. 清空绑定状态标志。
    s_sensor_map_bound = false;
}

bool mod_sensor_is_bound(void)
{
    // 1. 返回当前绑定状态。
    return s_sensor_map_bound;
}

/**
 * @brief 执行一次12路传感器采样
 * @param raw_data    输出位图（可为NULL）
 * @param sum_weight  输出权重和（可为NULL）
 */
static void sensor_sample(uint16_t *raw_data, float *sum_weight)
{
    uint16_t raw = 0U;
    float sum = 0.0f;

    // 1. 未绑定时直接输出默认值，避免访问无效映射。
    if (!mod_sensor_is_bound())
    {
        if (raw_data != NULL)
        {
            *raw_data = 0U;
        }
        if (sum_weight != NULL)
        {
            *sum_weight = 0.0f;
        }
        return;
    }

    // 2. 遍历12路通道，生成位图并累计权重。
    for (uint8_t i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++)
    {
        const mod_sensor_map_item_t *item = &s_sensor_map_active[i];

        if (drv_gpio_read(item->port, item->pin) == SENSOR_LINE_LEVEL)
        {
            raw |= (uint16_t)(1U << i);
            sum += item->factor;
        }
    }

    // 3. 回填原始位图。
    if (raw_data != NULL)
    {
        *raw_data = raw;
    }

    // 4. 回填权重和。
    if (sum_weight != NULL)
    {
        *sum_weight = sum;
    }
}

void mod_sensor_init(void)
{
    // 1. 初始化只清空运行缓存，不再自动绑定默认映射。
    s_current_weight = 0.0f;
    s_raw_data = 0U;
}

uint16_t mod_sensor_get_raw_data(void)
{
    // 1. 执行一次采样，仅获取位图。
    sensor_sample(&s_raw_data, NULL);

    // 2. 返回位图缓存。
    return s_raw_data;
}

float mod_sensor_get_weight(void)
{
    float sum_weight = 0.0f;

    // 1. 执行一次采样，得到权重和。
    sensor_sample(&s_raw_data, &sum_weight);

    // 2. 对权重值进行限幅，防止异常值扰动上层控制。
    if (sum_weight > 1.0f)
    {
        sum_weight = 1.0f;
    }
    else if (sum_weight < -1.0f)
    {
        sum_weight = -1.0f;
    }

    // 3. 更新缓存并返回。
    s_current_weight = sum_weight;
    return s_current_weight;
}