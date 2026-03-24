/**
 * @file    task_init.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   系统初始化任务实现。
 * @details
 * 1. InitTask 负责一次性完成模块绑定、初始化和上电安全态设置。
 * 2. 业务任务通过 task_wait_init_done 与初始化时序解耦。
 * 3. 初始化完成后释放 Sem_InitHandle 并自删除，避免重复执行。
 */

#include "task_init.h"

#include "main.h"
#include "adc.h"
#include "tim.h"
#include "usart.h"

#include "mod_battery.h"
#include "mod_k230.h"
#include "mod_key.h"
#include "mod_led.h"
#include "mod_motor.h"
#include "mod_relay.h"
#include "mod_sensor.h"
#include "mod_vofa.h"

#include "task_stepper.h"

#include <string.h>

/* 初始化闸门信号量：InitTask 释放，其他任务等待 */
extern osSemaphoreId_t Sem_InitHandle;
/* 串口发送互斥锁：VOFA/Stepper 绑定时注入 */
extern osMutexId_t PcMutexHandle;

/* ========================= GPIO 分支：LED 绑定表 ========================= */
static const mod_led_hw_cfg_t s_led_bind_map[LED_MAX] =
{
    [LED_RED] = {
        .port = LED_RED_GPIO_Port,
        .pin = LED_RED_Pin,
        .active_level = GPIO_LEVEL_LOW,
    },
    [LED_GREEN] = {
        .port = LED_GREEN_GPIO_Port,
        .pin = LED_GREEN_Pin,
        .active_level = GPIO_LEVEL_LOW,
    },
    [LED_YELLOW] = {
        .port = LED_YELLOW_GPIO_Port,
        .pin = LED_YELLOW_Pin,
        .active_level = GPIO_LEVEL_LOW,
    },
};

/* ========================= GPIO 分支：继电器绑定表 ========================= */
static const mod_relay_hw_cfg_t s_relay_bind_map[RELAY_MAX] =
{
    [RELAY_LASER] = {
        .port = Laser_GPIO_Port,
        .pin = Laser_Pin,
        .active_level = GPIO_LEVEL_HIGH,
    },
    [RELAY_BUZZER] = {
        .port = Buzzer_GPIO_Port,
        .pin = Buzzer_Pin,
        .active_level = GPIO_LEVEL_HIGH,
    },
};

/* ========================= GPIO 分支：按键绑定表 ========================= */
#define TASK_INIT_KEY_NUM (3U)
static const mod_key_hw_cfg_t s_key_bind_map[TASK_INIT_KEY_NUM] =
{
    [0] = {
        .port = KEY_1_GPIO_Port,
        .pin = KEY_1_Pin,
        .active_level = GPIO_LEVEL_LOW,
        .click_event = MOD_KEY_EVENT_1_CLICK,
        .double_event = MOD_KEY_EVENT_1_DOUBLE_CLICK,
        .long_event = MOD_KEY_EVENT_1_LONG_PRESS,
    },
    [1] = {
        .port = KEY_2_GPIO_Port,
        .pin = KEY_2_Pin,
        .active_level = GPIO_LEVEL_LOW,
        .click_event = MOD_KEY_EVENT_2_CLICK,
        .double_event = MOD_KEY_EVENT_2_DOUBLE_CLICK,
        .long_event = MOD_KEY_EVENT_2_LONG_PRESS,
    },
    [2] = {
        .port = KEY_3_GPIO_Port,
        .pin = KEY_3_Pin,
        .active_level = GPIO_LEVEL_LOW,
        .click_event = MOD_KEY_EVENT_3_CLICK,
        .double_event = MOD_KEY_EVENT_3_DOUBLE_CLICK,
        .long_event = MOD_KEY_EVENT_3_LONG_PRESS,
    },
};

/* ========================= 电机绑定表 ========================= */
static const mod_motor_hw_cfg_t s_motor_bind_map[MOD_MOTOR_MAX] =
{
    [MOD_MOTOR_LEFT] = {
        .in1_port = AIN2_GPIO_Port,
        .in1_pin = AIN2_Pin,
        .in2_port = AIN1_GPIO_Port,
        .in2_pin = AIN1_Pin,
        .pwm_htim = &htim4,
        .pwm_channel = TIM_CHANNEL_1,
        .pwm_invert = false,
        .enc_htim = &htim2,
        .enc_counter_bits = DRV_ENCODER_BITS_32,
        .enc_invert = false,
    },
    [MOD_MOTOR_RIGHT] = {
        .in1_port = BIN1_GPIO_Port,
        .in1_pin = BIN1_Pin,
        .in2_port = BIN2_GPIO_Port,
        .in2_pin = BIN2_Pin,
        .pwm_htim = &htim4,
        .pwm_channel = TIM_CHANNEL_2,
        .pwm_invert = false,
        .enc_htim = &htim3,
        .enc_counter_bits = DRV_ENCODER_BITS_16,
        .enc_invert = true,
    },
};

/* ========================= 12 路循迹传感器绑定表 ========================= */
static const mod_sensor_map_item_t s_sensor_bind_map[MOD_SENSOR_CHANNEL_NUM] =
{
    {GPIOG, GPIO_PIN_0, GPIO_LEVEL_HIGH, 0.60f},
    {GPIOG, GPIO_PIN_1, GPIO_LEVEL_HIGH, 0.40f},
    {GPIOG, GPIO_PIN_5, GPIO_LEVEL_HIGH, 0.30f},
    {GPIOG, GPIO_PIN_6, GPIO_LEVEL_HIGH, 0.20f},
    {GPIOG, GPIO_PIN_7, GPIO_LEVEL_HIGH, 0.10f},
    {GPIOG, GPIO_PIN_8, GPIO_LEVEL_HIGH, 0.05f},
    {GPIOG, GPIO_PIN_9, GPIO_LEVEL_HIGH, -0.05f},
    {GPIOG, GPIO_PIN_10, GPIO_LEVEL_HIGH, -0.10f},
    {GPIOG, GPIO_PIN_11, GPIO_LEVEL_HIGH, -0.20f},
    {GPIOG, GPIO_PIN_12, GPIO_LEVEL_HIGH, -0.30f},
    {GPIOG, GPIO_PIN_13, GPIO_LEVEL_HIGH, -0.40f},
    {GPIOG, GPIO_PIN_14, GPIO_LEVEL_HIGH, -0.60f},
};

/**
 * @brief 按配置表绑定默认模块上下文。
 * @details
 * 该函数只做“配置注入”，不执行硬件动作。
 */
static void task_init_bind_map(void)
{
    mod_led_ctx_t *led_ctx = mod_led_get_default_ctx();             /* LED 默认上下文 */
    mod_relay_ctx_t *relay_ctx = mod_relay_get_default_ctx();       /* Relay 默认上下文 */
    mod_sensor_ctx_t *sensor_ctx = mod_sensor_get_default_ctx();     /* Sensor 默认上下文 */
    mod_key_ctx_t *key_ctx = mod_key_get_default_ctx();              /* Key 默认上下文 */
    mod_motor_ctx_t *motor_ctx = mod_motor_get_default_ctx();        /* Motor 默认上下文 */
    mod_battery_ctx_t *battery_ctx = mod_battery_get_default_ctx();  /* Battery 默认上下文 */

    mod_led_bind_t led_bind;            /* LED 绑定参数 */
    mod_relay_bind_t relay_bind;        /* Relay 绑定参数 */
    mod_sensor_bind_t sensor_bind;      /* Sensor 绑定参数 */
    mod_key_bind_t key_bind;            /* Key 绑定参数 */
    mod_motor_bind_t motor_bind;        /* Motor 绑定参数 */
    mod_battery_bind_t battery_bind;    /* Battery 绑定参数 */

    led_bind.map = s_led_bind_map;
    led_bind.map_num = LED_MAX;
    (void)mod_led_ctx_init(led_ctx, &led_bind);

    relay_bind.map = s_relay_bind_map;
    relay_bind.map_num = RELAY_MAX;
    (void)mod_relay_ctx_init(relay_ctx, &relay_bind);

    sensor_bind.map = s_sensor_bind_map;
    sensor_bind.map_num = MOD_SENSOR_CHANNEL_NUM;
    (void)mod_sensor_ctx_init(sensor_ctx, &sensor_bind);

    key_bind.map = s_key_bind_map;
    key_bind.key_num = TASK_INIT_KEY_NUM;
    (void)mod_key_ctx_init(key_ctx, &key_bind);

    motor_bind.map = s_motor_bind_map;
    motor_bind.map_num = MOD_MOTOR_MAX;
    (void)mod_motor_ctx_init(motor_ctx, &motor_bind);

    battery_bind.hadc = &hadc1;
    battery_bind.adc_ref_v = MOD_BATTERY_DEFAULT_ADC_REF_V;
    battery_bind.adc_res = MOD_BATTERY_DEFAULT_ADC_RES;
    battery_bind.voltage_ratio = MOD_BATTERY_DEFAULT_VOL_RATIO;
    battery_bind.sample_cnt = MOD_BATTERY_DEFAULT_SAMPLE_CNT;
    (void)mod_battery_ctx_init(battery_ctx, &battery_bind);
}

/**
 * @brief 执行基础模块初始化并写入上电安全状态。
 */
static void task_init_modules(void)
{
    mod_led_ctx_t *led_ctx = mod_led_get_default_ctx();         /* LED 默认上下文 */
    mod_relay_ctx_t *relay_ctx = mod_relay_get_default_ctx();   /* Relay 默认上下文 */
    mod_sensor_ctx_t *sensor_ctx = mod_sensor_get_default_ctx(); /* Sensor 默认上下文 */
    mod_motor_ctx_t *motor_ctx = mod_motor_get_default_ctx();   /* Motor 默认上下文 */

    mod_led_init(led_ctx);
    mod_relay_init(relay_ctx);
    mod_sensor_init(sensor_ctx);

    /* 上电默认关闭激光继电器。 */
    mod_relay_off(relay_ctx, RELAY_LASER);

    mod_motor_init(motor_ctx);
    mod_motor_set_mode(motor_ctx, MOD_MOTOR_LEFT, MOTOR_MODE_DRIVE);
    mod_motor_set_mode(motor_ctx, MOD_MOTOR_RIGHT, MOTOR_MODE_DRIVE);
}

/**
 * @brief 绑定或重绑定 VOFA 默认上下文。
 */
static void task_init_vofa_bind(void)
{
    mod_vofa_ctx_t *vofa_ctx = mod_vofa_get_default_ctx(); /* VOFA 默认上下文 */
    mod_vofa_bind_t vofa_bind;                              /* VOFA 绑定参数 */

    memset(&vofa_bind, 0, sizeof(vofa_bind));
    vofa_bind.huart = &huart3;
    vofa_bind.tx_mutex = PcMutexHandle;
    vofa_bind.sem_count = 0U;

    if (!vofa_ctx->inited)
    {
        (void)mod_vofa_ctx_init(vofa_ctx, &vofa_bind);
    }
    else
    {
        (void)mod_vofa_bind(vofa_ctx, &vofa_bind);
    }
}

/**
 * @brief 绑定或重绑定 K230 默认上下文。
 */
static void task_init_k230_bind(void)
{
    mod_k230_ctx_t *k230_ctx = mod_k230_get_default_ctx(); /* K230 默认上下文 */
    mod_k230_bind_t k230_bind;                              /* K230 绑定参数 */

    memset(&k230_bind, 0, sizeof(k230_bind));
    k230_bind.huart = &huart4;
    k230_bind.sem_count = 0U;
    k230_bind.tx_mutex = NULL;
    k230_bind.checksum_algo = MOD_K230_CHECKSUM_XOR;

    if (!k230_ctx->inited)
    {
        (void)mod_k230_ctx_init(k230_ctx, &k230_bind);
    }
    else
    {
        (void)mod_k230_bind(k230_ctx, &k230_bind);
    }
}

/**
 * @brief 初始化 Stepper 双轴通道。
 */
static void task_init_stepper_bind(void)
{
    (void)task_stepper_prepare_channels();
}

/**
 * @brief 初始化闸门等待函数。
 * @details
 * 首次通过会回填一次信号量，使后续任务快速通过。
 */
void task_wait_init_done(void)
{
    if (Sem_InitHandle == NULL)
    {
        return;
    }

    if (osSemaphoreAcquire(Sem_InitHandle, osWaitForever) == osOK)
    {
        (void)osSemaphoreRelease(Sem_InitHandle);
    }
}

/**
 * @brief InitTask 入口：执行一次性初始化并释放闸门。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartInitTask(void *argument)
{
    (void)argument;

    /* 步骤1：完成默认模块上下文的绑定参数注入。 */
    task_init_bind_map();
    /* 步骤2：执行基础模块初始化并写入上电安全态。 */
    task_init_modules();

    /* 步骤3：绑定通信模块与步进双轴通道。 */
    task_init_vofa_bind();
    task_init_k230_bind();
    task_init_stepper_bind();

    /* 步骤4：释放初始化闸门，允许其他业务任务进入主循环。 */
    if (Sem_InitHandle != NULL)
    {
        (void)osSemaphoreRelease(Sem_InitHandle);
    }

    /* 步骤5：一次性初始化任务执行完毕后自删除。 */
    (void)osThreadTerminate(osThreadGetId());
}

