#include "mod_sensor.h"

#include <string.h>

/* 最近一次计算得到的归一化权重值 */
static float s_current_weight = 0.0f;
/* 最近一次采样得到的 12 路状态（黑线=1） */
static uint8_t s_current_states[MOD_SENSOR_CHANNEL_NUM];
/* 当前生效的 12 路映射配置 */
static mod_sensor_map_item_t s_sensor_map_active[MOD_SENSOR_CHANNEL_NUM];
/* 映射是否已绑定 */
static bool s_sensor_map_bound = false;

/* 检查单路映射项是否合法 */
static bool mod_sensor_check_item(const mod_sensor_map_item_t *item)
{
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
static void sensor_sample(uint8_t *states_out, float *sum_weight)
{
    uint8_t sampled_states[MOD_SENSOR_CHANNEL_NUM];
    float sum = 0.0f;
    uint8_t i;

    memset(sampled_states, 0, sizeof(sampled_states));

    if (mod_sensor_is_bound())
    {
        for (i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++)
        {
            const mod_sensor_map_item_t *item = &s_sensor_map_active[i];
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

bool mod_sensor_bind_map(const mod_sensor_map_item_t *map, uint8_t map_num)
{
    uint8_t i;

    if ((map == NULL) || (map_num != MOD_SENSOR_CHANNEL_NUM))
    {
        return false;
    }

    for (i = 0U; i < MOD_SENSOR_CHANNEL_NUM; i++)
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

void mod_sensor_unbind_map(void)
{
    s_sensor_map_bound = false;
}

bool mod_sensor_is_bound(void)
{
    return s_sensor_map_bound;
}

void mod_sensor_init(void)
{
    s_current_weight = 0.0f;
    memset(s_current_states, 0, sizeof(s_current_states));
}

bool mod_sensor_get_states(uint8_t *states, uint8_t states_num)
{
    if ((states == NULL) || (states_num < MOD_SENSOR_CHANNEL_NUM))
    {
        return false;
    }

    sensor_sample(states, NULL);
    return mod_sensor_is_bound();
}

float mod_sensor_get_weight(void)
{
    float sum_weight = 0.0f;

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
