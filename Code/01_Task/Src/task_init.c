#include "task_init.h"

#include "main.h"
#include "tim.h"
#include "usart.h"
#include "mod_key.h"
#include "mod_led.h"
#include "mod_motor.h"
#include "mod_relay.h"
#include "mod_sensor.h"
#include "mod_vofa.h"
#include <string.h>

/**
 * @brief freertos.c 中创建的初始化门控信号量。
 *
 * @details
 * Sem_Init 作为系统“初始化完成闸门”使用：
 * 1. 初始计数为 0，所有业务任务在 task_wait_init_done() 阻塞。
 * 2. InitTask 全部初始化完成后释放一次。
 * 3. 首个通过者会再释放一次，使该闸门保持常开。
 */
extern osSemaphoreId_t Sem_InitHandle;

/**
 * @brief freertos.c 中创建的串口发送互斥锁。
 *
 * @details
 * DCC/Stepper 等任务都可能调用 VOFA 发送接口，需要共享此互斥锁保护 DMA 发送。
 */
extern osMutexId_t PcMutexHandle;

/* ========================= 硬件绑定表：LED ========================= */
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

/* ========================= 硬件绑定表：继电器 ========================= */
static const mod_relay_hw_cfg_t s_relay_bind_map[RELAY_MAX] =
{
    [RELAY_LASER] = {
        .port = Laser_GPIO_Port,
        .pin = Laser_Pin,
        .active_level = GPIO_LEVEL_HIGH,
    },
};

/* ========================= 硬件绑定表：底盘双电机 ========================= */
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

/* ========================= 硬件绑定表：12 路循迹传感器 ========================= */
static const mod_sensor_map_item_t s_sensor_bind_map[MOD_SENSOR_CHANNEL_NUM] =
{
    {GPIOG, GPIO_PIN_0, -0.60f},
    {GPIOG, GPIO_PIN_1, -0.40f},
    {GPIOG, GPIO_PIN_5, -0.30f},
    {GPIOG, GPIO_PIN_6, -0.20f},
    {GPIOG, GPIO_PIN_7, -0.10f},
    {GPIOG, GPIO_PIN_8, -0.05f},
    {GPIOG, GPIO_PIN_9, 0.05f},
    {GPIOG, GPIO_PIN_10, 0.10f},
    {GPIOG, GPIO_PIN_11, 0.20f},
    {GPIOG, GPIO_PIN_12, 0.30f},
    {GPIOG, GPIO_PIN_13, 0.40f},
    {GPIOG, GPIO_PIN_14, 0.60f},
};

/**
 * @brief 绑定非 UART / 非 OLED 模块的硬件映射。
 */
static void task_init_bind_map(void)
{
    (void)mod_led_bind_map(s_led_bind_map, LED_MAX);
    (void)mod_relay_bind_map(s_relay_bind_map, RELAY_MAX);
    (void)mod_motor_bind_map(s_motor_bind_map, MOD_MOTOR_MAX);
    (void)mod_sensor_bind_map(s_sensor_bind_map, MOD_SENSOR_CHANNEL_NUM);
}

/**
 * @brief 初始化非 UART / 非 OLED 模块的运行状态。
 */
static void task_init_modules(void)
{
    mod_led_Init();
    mod_relay_init();
    mod_key_init();

    mod_motor_init();
    mod_sensor_init();
    mod_motor_set_mode(MOD_MOTOR_LEFT, MOTOR_MODE_DRIVE);
    mod_motor_set_mode(MOD_MOTOR_RIGHT, MOTOR_MODE_DRIVE);
}

/**
 * @brief 在 InitTask 中完成 VOFA + 串口绑定。
 *
 * @details
 * 本项目不再使用 PC 任务中的 start/stop 命令处理逻辑，但仍需要 VOFA 输出数据。
 * 因此这里在系统初始化阶段直接绑定：
 * 1. 串口：USART3（huart3）。
 * 2. 互斥锁：PcMutexHandle（用于多任务发送保护）。
 * 3. 信号量列表：留空（sem_count = 0），即不再消费命令事件。
 */
static void task_init_vofa_bind(void)
{
    mod_vofa_ctx_t *vofa_ctx;
    mod_vofa_bind_t vofa_bind;

    vofa_ctx = mod_vofa_get_default_ctx();
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

void StartInitTask(void *argument)
{
    (void)argument;

    task_init_bind_map();
    task_init_modules();

    /* 在初始化任务中完成 VOFA 串口绑定，替代原 PcTask 的绑定职责。 */
    task_init_vofa_bind();

    if (Sem_InitHandle != NULL)
    {
        (void)osSemaphoreRelease(Sem_InitHandle);
    }

    (void)osThreadTerminate(osThreadGetId());
}
