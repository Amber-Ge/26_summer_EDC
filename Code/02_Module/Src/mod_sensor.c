/**
 * @file    mod_sensor.c
 * @brief   循迹传感器模块实现。
 * @details
 * 1. 文件作用：实现多路循迹传感器采样、黑线状态判定和权重计算。
 * 2. 解耦边界：仅提供“状态/权重”数据，不包含底盘控制输出决策。
 * 3. 上层绑定：`DccTask` 等控制任务读取结果并执行业务闭环。
 * 4. 下层依赖：`drv_gpio` 读取各传感器输入电平。
 */
#include "mod_sensor.h"

#include <string.h>

/* 最近一次计算得到的归一化权重值 */
static float s_current_weight = 0.0f; // 局部业务变量
/* 最近一次采样得到的 12 路状态（黑线=1） */
static uint8_t s_current_states[MOD_SENSOR_CHANNEL_NUM]; // 传感器当前采样状态缓存。
/* 当前生效的 12 路映射配置 */
static mod_sensor_map_item_t s_sensor_map_active[MOD_SENSOR_CHANNEL_NUM]; // 传感器通道映射表的运行态副本。
/* 映射是否已绑定 */
static bool s_sensor_map_bound = false; // 局部业务变量

/* 检查单路映射项是否合法 */
/**
 * @brief 执行模块层设备控制与状态管理。
 * @param item 函数输入参数，语义由调用场景决定。
 * @return 布尔结果，`true` 表示满足条件。
 */
static bool mod_sensor_check_item(const mod_sensor_map_item_t *item)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if (item == NULL)
    {
        return false;
    }
    if (item->port == NULL)
    {
        return false;
    }
    if ((item->line_level != GPIO_LEVEL_LOW) && (item->line_level != GPIO_LEVEL_HIGH))
    {
        return false;
    }
    return true;
}

/* 执行一次 12 路采样，并可选输出状态与权重和 */
/**
 * @brief 执行当前函数对应的业务处理逻辑。
 * @param states_out 状态或模式控制参数。
 * @param sum_weight 函数输入参数，语义由调用场景决定。
 * @return 无。
 */
static void sensor_sample(uint8_t *states_out, float *sum_weight)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint8_t sampled_states[MOD_SENSOR_CHANNEL_NUM]; // 传感器采样状态缓存
    float sum = 0.0f; // 累加或统计变量
    uint8_t i; // 循环或计数变量

    memset(sampled_states, 0, sizeof(sampled_states));

    if (mod_sensor_is_bound())
    {
        for (i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++) // 循环计数器
        {
            const mod_sensor_map_item_t *item = &s_sensor_map_active[i]; // 模块变量，用于保存运行时状态。
            if (drv_gpio_read(item->port, item->pin) == item->line_level)
            {
                sampled_states[i] = 1U; /* 检测到黑线 */
                sum += item->factor;
            }
        }
    }

    memcpy(s_current_states, sampled_states, sizeof(s_current_states));
    if (states_out != NULL)
    {
        memcpy(states_out, sampled_states, MOD_SENSOR_CHANNEL_NUM);
    }
    if (sum_weight != NULL)
    {
        *sum_weight = sum;
    }
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param map 函数输入参数，语义由调用场景决定。
 * @param map_num 数据长度或数量参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_sensor_bind_map(const mod_sensor_map_item_t *map, uint8_t map_num)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint8_t i; // 循环或计数变量

    if ((map == NULL) || (map_num != MOD_SENSOR_CHANNEL_NUM))
    {
        return false;
    }

    for (i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++) // 循环计数器
    {
        if (!mod_sensor_check_item(&map[i]))
        {
            return false;
        }
    }

    memcpy(s_sensor_map_active, map, sizeof(s_sensor_map_active));
    s_sensor_map_bound = true;
    return true;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 无。
 */
void mod_sensor_unbind_map(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    s_sensor_map_bound = false;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_sensor_is_bound(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return s_sensor_map_bound;
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 无。
 */
void mod_sensor_init(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    s_current_weight = 0.0f;
    memset(s_current_states, 0, sizeof(s_current_states));
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param states 状态或模式控制参数。
 * @param states_num 数据长度或数量参数。
 * @return 布尔结果，`true` 表示满足条件。
 */
bool mod_sensor_get_states(uint8_t *states, uint8_t states_num)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if ((states == NULL) || (states_num < MOD_SENSOR_CHANNEL_NUM))
    {
        return false;
    }

    sensor_sample(states, NULL);
    return mod_sensor_is_bound();
}

/**
 * @brief 执行模块层设备控制与状态管理。
 * @param 无。
 * @return 返回计算结果或状态码，具体语义见实现。
 */
float mod_sensor_get_weight(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    float sum_weight = 0.0f; // 累加或统计变量

    sensor_sample(NULL, &sum_weight);

    if (sum_weight > 1.0f)
    {
        sum_weight = 1.0f;
    }
    else if (sum_weight < -1.0f)
    {
        sum_weight = -1.0f;
    }

    s_current_weight = sum_weight;
    return s_current_weight;
}



