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
#include "mod_k230.h"
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

/**
 * @brief 在 InitTask 中完成 K230 协议层与串口资源绑定。
 *
 * @details
 * 设计说明（与当前“解耦版 mod_k230”接口保持一致）：
 * 1. 本函数只负责“装配（bind）”，不写入任何业务逻辑。
 * 2. K230 采用 ctx + bind 模型，InitTask 作为系统装配层注入硬件资源。
 * 3. 绑定信息包括：
 *    - UART：huart4（当前工程中 VOFA 使用 huart3，Stepper 使用 huart5/huart2，
 *      因此此处选用未占用的 huart4，避免 UART ownership 冲突）。
 *    - 校验算法：MOD_K230_CHECKSUM_XOR（当前模块唯一已实现算法）。
 *    - 通知信号量：不绑定（sem_count = 0），后续若有消费者任务可再动态追加。
 *    - 发送互斥锁：不绑定（tx_mutex = NULL），保持最小耦合。
 *
 * 4. 初始化策略与 VOFA 保持一致：
 *    - 若默认 ctx 尚未 init，则执行 ctx_init(ctx, &bind)；
 *    - 若已 init，则执行 rebind(ctx, &bind)。
 *
 * 5. 无论绑定成功与否，InitTask 都继续执行后续流程，不在这里阻塞系统启动。
 *    错误处理由模块内部“返回值 + 资源回滚”保证安全性。
 */
static void task_init_k230_bind(void)
{
    mod_k230_ctx_t *k230_ctx;
    mod_k230_bind_t k230_bind;

    k230_ctx = mod_k230_get_default_ctx();
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
    task_init_k230_bind();

    if (Sem_InitHandle != NULL)
    {
        (void)osSemaphoreRelease(Sem_InitHandle);
    }

    (void)osThreadTerminate(osThreadGetId());
}
