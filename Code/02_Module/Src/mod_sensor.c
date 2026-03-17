#include "mod_sensor.h"

#define SENSOR_LINE_LEVEL GPIO_LEVEL_LOW // 寻线传感器有效电平定义（低电平表示检测到线）

static float s_current_weight = 0.0f; // 最近一次计算得到的归一化权重
static uint16_t s_raw_data = 0; // 最近一次采样得到的传感器位图

typedef struct
{
    GPIO_TypeDef* port; // 传感器连接的GPIO端口
    uint16_t pin; // 传感器连接的GPIO引脚
    float factor; // 该传感器对应的权重系数
} sensor_cfg_t;

static const sensor_cfg_t sensor_map[12] = // 12 路传感器硬件映射及权重系数表
{
    {GPIOG, GPIO_PIN_0,  -0.60f},
    {GPIOG, GPIO_PIN_1,  -0.40f},
    {GPIOG, GPIO_PIN_5,  -0.30f},
    {GPIOG, GPIO_PIN_6,  -0.20f},
    {GPIOG, GPIO_PIN_7,  -0.10f},
    {GPIOG, GPIO_PIN_8,  -0.05f},
    {GPIOG, GPIO_PIN_9,   0.05f},
    {GPIOG, GPIO_PIN_10,  0.10f},
    {GPIOG, GPIO_PIN_11,  0.20f},
    {GPIOG, GPIO_PIN_12,  0.30f},
    {GPIOG, GPIO_PIN_13,  0.40f},
    {GPIOG, GPIO_PIN_14,  0.60f}
};

static void sensor_sample(uint16_t *raw_data, float *sum_weight)
{
    uint16_t raw = 0; // 采样得到的传感器位图
    float sum = 0.0f; // 被触发通道的权重累加值

    //1. 扫描全部 12 路传感器，更新位图并累计权重
    for (int i = 0; i < 12; i++) // i：传感器索引
    {
        if (drv_gpio_read(sensor_map[i].port, sensor_map[i].pin) == SENSOR_LINE_LEVEL)
        {
            raw |= (uint16_t)(1U << i);
            sum += sensor_map[i].factor;
        }
    }

    //2. 按需输出位图结果
    if (raw_data != NULL)
    {
        *raw_data = raw;
    }

    //3. 按需输出权重和结果
    if (sum_weight != NULL)
    {
        *sum_weight = sum;
    }
}

void mod_sensor_init(void)
{
    //1. 清空模块缓存状态
    s_current_weight = 0.0f;
    s_raw_data = 0;
}

uint16_t mod_sensor_get_raw_data(void)
{
    //1. 执行一次采样，仅获取位图结果
    sensor_sample(&s_raw_data, NULL);

    //2. 返回缓存的原始位图
    return s_raw_data;
}

float mod_sensor_get_weight(void)
{
    float sum_weight = 0.0f; // 本次采样得到的权重和

    //1. 执行一次采样，更新位图与权重和
    sensor_sample(&s_raw_data, &sum_weight);

    //2. 权重限幅到 [-1.0, 1.0]，避免异常值影响上层控制
    if (sum_weight > 1.0f)
        sum_weight = 1.0f;
    else if (sum_weight < -1.0f)
        sum_weight = -1.0f;

    //3. 更新缓存并返回
    s_current_weight = sum_weight;
    return s_current_weight;
}
