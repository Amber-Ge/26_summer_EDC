/**
 * @file    task_init.c
 * @author  Jiang Kaizhong
 * @version v1.00
 * @date    2026-04-16
 * @brief   System assembly entry for one-shot startup binding and init.
 */

#include "task_init.h"

#include <string.h>

#include "adc.h"
#include "i2c.h"
#include "main.h"
#include "tim.h"
#include "usart.h"

#include "mod_battery.h"
#include "mod_k230.h"
#include "mod_key.h"
#include "mod_led.h"
#include "mod_motor.h"
#include "mod_oled.h"
#include "mod_relay.h"
#include "mod_sensor.h"
#include "mod_stepper.h"
#include "mod_vision.h"
#include "mod_vofa.h"

#include "drv_uart.h"

/* InitTask releases this gate; other tasks wait for it before entering work loops. */
extern osSemaphoreId_t Sem_InitHandle;
/* Shared UART TX mutex created by freertos.c and injected into communication modules. */
extern osMutexId_t PcMutexHandle;

/* ========================= Stepper serial bind parameters ========================= */
#define TASK_INIT_STEPPER_ADDR_1 (1U)
#define TASK_INIT_STEPPER_ADDR_2 (2U)

/* ========================= GPIO branch: LED bind map ========================= */
static const mod_led_hw_cfg_t s_led_bind_map[LED_MAX] =
{
    [LED_RED] = {
        .pin = {
            .port = State_LED_GPIO_Port,
            .pin = State_LED_Pin,
        },
        .active_level = GPIO_LEVEL_HIGH,
    },
    [LED_GREEN] = {
        .pin = {
            .port = State_LED_GPIO_Port,
            .pin = State_LED_Pin,
        },
        .active_level = GPIO_LEVEL_HIGH,
    },
    [LED_YELLOW] = {
        .pin = {
            .port = State_LED_GPIO_Port,
            .pin = State_LED_Pin,
        },
        .active_level = GPIO_LEVEL_HIGH,
    },
    [LED_BROAD] = {
        .pin = {
            .port = Led_broad_GPIO_Port,
            .pin = Led_broad_Pin,
        },
        .active_level = GPIO_LEVEL_HIGH,
    },
};

/* ========================= GPIO branch: relay bind map ========================= */
static const mod_relay_hw_cfg_t s_relay_bind_map[RELAY_MAX] =
{
    [RELAY_LASER] = {
        .pin = {
            .port = laser_GPIO_Port,
            .pin = laser_Pin,
        },
        .active_level = GPIO_LEVEL_HIGH,
    },
    [RELAY_BUZZER] = {
        .pin = {
            .port = buzzer_GPIO_Port,
            .pin = buzzer_Pin,
        },
        .active_level = GPIO_LEVEL_HIGH,
    },
};

/* ========================= GPIO branch: key bind map ========================= */
#define TASK_INIT_KEY_NUM (3U)
static const mod_key_hw_cfg_t s_key_bind_map[TASK_INIT_KEY_NUM] =
{
    [0] = {
        .pin = {
            .port = Key1_GPIO_Port,
            .pin = Key1_Pin,
        },
        .active_level = GPIO_LEVEL_LOW,
        .click_event = MOD_KEY_EVENT_1_CLICK,
        .double_event = MOD_KEY_EVENT_1_DOUBLE_CLICK,
        .long_event = MOD_KEY_EVENT_1_LONG_PRESS,
    },
    [1] = {
        .pin = {
            .port = Key2_GPIO_Port,
            .pin = Key2_Pin,
        },
        .active_level = GPIO_LEVEL_LOW,
        .click_event = MOD_KEY_EVENT_2_CLICK,
        .double_event = MOD_KEY_EVENT_2_DOUBLE_CLICK,
        .long_event = MOD_KEY_EVENT_2_LONG_PRESS,
    },
    [2] = {
        .pin = {
            .port = Key3_GPIO_Port,
            .pin = Key3_Pin,
        },
        .active_level = GPIO_LEVEL_LOW,
        .click_event = MOD_KEY_EVENT_3_CLICK,
        .double_event = MOD_KEY_EVENT_3_DOUBLE_CLICK,
        .long_event = MOD_KEY_EVENT_3_LONG_PRESS,
    },
};

/* ========================= Motor bind map ========================= */
static const mod_motor_hw_cfg_t s_motor_bind_map[MOD_MOTOR_MAX] =
{
    [MOD_MOTOR_LEFT] = {
        .in1 = {
            .port = AIN2_GPIO_Port,
            .pin = AIN2_Pin,
        },
        .in2 = {
            .port = AIN1_GPIO_Port,
            .pin = AIN1_Pin,
        },
        .pwm = {
            .instance = &htim3,
            .channel = TIM_CHANNEL_1,
            .duty_max = MOD_MOTOR_DUTY_MAX,
            .invert = false,
        },
        .encoder = {
            .instance = &htim1,
            .counter_bits = DRV_ENCODER_BITS_16,
            .invert = false,
        },
    },
    [MOD_MOTOR_RIGHT] = {
        .in1 = {
            .port = BIN1_GPIO_Port,
            .pin = BIN1_Pin,
        },
        .in2 = {
            .port = BIN2_GPIO_Port,
            .pin = BIN2_Pin,
        },
        .pwm = {
            .instance = &htim3,
            .channel = TIM_CHANNEL_2,
            .duty_max = MOD_MOTOR_DUTY_MAX,
            .invert = false,
        },
        .encoder = {
            .instance = &htim5,
            .counter_bits = DRV_ENCODER_BITS_32,
            .invert = true,
        },
    },
};

/* ========================= 8-channel line sensor bind map ========================= */
static const mod_sensor_map_item_t s_sensor_bind_map[MOD_SENSOR_CHANNEL_NUM] =
{
    [0] = {
        .pin = {
            .port = L1_GPIO_Port,
            .pin = L1_Pin,
        },
        .line_level = GPIO_LEVEL_HIGH,
        .factor = 0.40f,
    },
    [1] = {
        .pin = {
            .port = L2_GPIO_Port,
            .pin = L2_Pin,
        },
        .line_level = GPIO_LEVEL_HIGH,
        .factor = 0.20f,
    },
    [2] = {
        .pin = {
            .port = L3_GPIO_Port,
            .pin = L3_Pin,
        },
        .line_level = GPIO_LEVEL_HIGH,
        .factor = 0.10f,
    },
    [3] = {
        .pin = {
            .port = L4_GPIO_Port,
            .pin = L4_Pin,
        },
        .line_level = GPIO_LEVEL_HIGH,
        .factor = 0.05f,
    },
    [4] = {
        .pin = {
            .port = R4_GPIO_Port,
            .pin = R4_Pin,
        },
        .line_level = GPIO_LEVEL_HIGH,
        .factor = -0.05f,
    },
    [5] = {
        .pin = {
            .port = R3_GPIO_Port,
            .pin = R3_Pin,
        },
        .line_level = GPIO_LEVEL_HIGH,
        .factor = -0.10f,
    },
    [6] = {
        .pin = {
            .port = R2_GPIO_Port,
            .pin = R2_Pin,
        },
        .line_level = GPIO_LEVEL_HIGH,
        .factor = -0.20f,
    },
    [7] = {
        .pin = {
            .port = R1_GPIO_Port,
            .pin = R1_Pin,
        },
        .line_level = GPIO_LEVEL_HIGH,
        .factor = -0.40f,
    },
};

/**
 * @brief Inject static board mapping into default module contexts.
 * @details
 * This step only assembles resource relations and performs no hardware action.
 */
static void task_init_bind_map(void)
{
    mod_led_ctx_t *led_ctx = mod_led_get_default_ctx();
    mod_relay_ctx_t *relay_ctx = mod_relay_get_default_ctx();
    mod_sensor_ctx_t *sensor_ctx = mod_sensor_get_default_ctx();
    mod_key_ctx_t *key_ctx = mod_key_get_default_ctx();
    mod_motor_ctx_t *motor_ctx = mod_motor_get_default_ctx();
    mod_battery_ctx_t *battery_ctx = mod_battery_get_default_ctx();

    mod_led_bind_t led_bind;
    mod_relay_bind_t relay_bind;
    mod_sensor_bind_t sensor_bind;
    mod_key_bind_t key_bind;
    mod_motor_bind_t motor_bind;
    mod_battery_bind_t battery_bind;

    memset(&led_bind, 0, sizeof(led_bind));
    led_bind.map = s_led_bind_map;
    led_bind.map_num = LED_MAX;
    (void)mod_led_ctx_init(led_ctx, &led_bind);

    memset(&relay_bind, 0, sizeof(relay_bind));
    relay_bind.map = s_relay_bind_map;
    relay_bind.map_num = RELAY_MAX;
    (void)mod_relay_ctx_init(relay_ctx, &relay_bind);

    memset(&sensor_bind, 0, sizeof(sensor_bind));
    sensor_bind.map = s_sensor_bind_map;
    sensor_bind.map_num = MOD_SENSOR_CHANNEL_NUM;
    (void)mod_sensor_ctx_init(sensor_ctx, &sensor_bind);

    memset(&key_bind, 0, sizeof(key_bind));
    key_bind.map = s_key_bind_map;
    key_bind.key_num = TASK_INIT_KEY_NUM;
    (void)mod_key_ctx_init(key_ctx, &key_bind);

    memset(&motor_bind, 0, sizeof(motor_bind));
    motor_bind.map = s_motor_bind_map;
    motor_bind.map_num = MOD_MOTOR_MAX;
    (void)mod_motor_ctx_init(motor_ctx, &motor_bind);

    memset(&battery_bind, 0, sizeof(battery_bind));
    battery_bind.hadc = &hadc1;
    battery_bind.adc_ref_v = MOD_BATTERY_DEFAULT_ADC_REF_V;
    battery_bind.adc_res = MOD_BATTERY_DEFAULT_ADC_RES;
    battery_bind.voltage_ratio = MOD_BATTERY_DEFAULT_VOL_RATIO;
    battery_bind.sample_cnt = MOD_BATTERY_DEFAULT_SAMPLE_CNT;
    (void)mod_battery_ctx_init(battery_ctx, &battery_bind);
}

/**
 * @brief Initialize basic modules and write explicit power-on safe states.
 */
static void task_init_modules(void)
{
    mod_led_ctx_t *led_ctx = mod_led_get_default_ctx();
    mod_relay_ctx_t *relay_ctx = mod_relay_get_default_ctx();
    mod_sensor_ctx_t *sensor_ctx = mod_sensor_get_default_ctx();
    mod_motor_ctx_t *motor_ctx = mod_motor_get_default_ctx();

    /* Bring simple GPIO modules into known output/input runtime state. */
    mod_led_init(led_ctx);
    mod_relay_init(relay_ctx);
    mod_sensor_init(sensor_ctx);

    /* Explicit power-on safe state: keep laser and buzzer off. */
    mod_relay_off(relay_ctx, RELAY_LASER);
    mod_relay_off(relay_ctx, RELAY_BUZZER);

    /* Start motor runtime objects, then switch into drive-ready zero-output state. */
    mod_motor_init(motor_ctx);
    mod_motor_set_mode(motor_ctx, MOD_MOTOR_LEFT, MOTOR_MODE_DRIVE);
    mod_motor_set_mode(motor_ctx, MOD_MOTOR_RIGHT, MOTOR_MODE_DRIVE);
    mod_motor_set_duty(motor_ctx, MOD_MOTOR_LEFT, 0);
    mod_motor_set_duty(motor_ctx, MOD_MOTOR_RIGHT, 0);
}

/**
 * @brief Bind or rebind the default VOFA communication context.
 */
static void task_init_vofa_bind(void)
{
    mod_vofa_ctx_t *vofa_ctx = mod_vofa_get_default_ctx();
    mod_vofa_bind_t vofa_bind;

    memset(&vofa_bind, 0, sizeof(vofa_bind));
    /* VOFA debug channel: USART1, PA9 TX / PA10 RX. */
    vofa_bind.huart = &huart1;
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
 * @brief Bind or rebind the default K230 communication context.
 */
static void task_init_k230_bind(void)
{
    mod_k230_ctx_t *k230_ctx = mod_k230_get_default_ctx();
    mod_k230_bind_t k230_bind;

    memset(&k230_bind, 0, sizeof(k230_bind));
    /* K230 vision link: USART2, PD5 TX / PD6 RX. */
    k230_bind.huart = &huart2;
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
 * @brief 初始化统一视觉语义模块默认上下文。
 */
static void task_init_vision_module(void)
{
    mod_vision_ctx_t *vision_ctx = mod_vision_get_default_ctx();

    if (!vision_ctx->inited)
    {
        (void)mod_vision_ctx_init(vision_ctx);
    }
    else
    {
        mod_vision_clear(vision_ctx);
    }
}

/**
 * @brief Bind dual serial stepper channels to USART3 / USART6.
 * @details
 * This project keeps the old serial-control route for the stepper drivers.
 */
static void task_init_stepper_bind(void)
{
    mod_stepper_ctx_t *stepper1_ctx = mod_stepper_get_default_ctx(MOD_STEPPER_AXIS_1);
    mod_stepper_ctx_t *stepper2_ctx = mod_stepper_get_default_ctx(MOD_STEPPER_AXIS_2);
    mod_stepper_bind_t stepper1_bind;
    mod_stepper_bind_t stepper2_bind;

    memset(&stepper1_bind, 0, sizeof(stepper1_bind));
    /* Stepper axis 1: USART3, PC10 TX / PC11 RX. */
    stepper1_bind.huart = &huart3;
    stepper1_bind.tx_mutex = PcMutexHandle;
    stepper1_bind.driver_addr = TASK_INIT_STEPPER_ADDR_1;
    (void)mod_stepper_ctx_init(stepper1_ctx, &stepper1_bind);

    memset(&stepper2_bind, 0, sizeof(stepper2_bind));
    /* Stepper axis 2: USART6, PC6 TX / PC7 RX. */
    stepper2_bind.huart = &huart6;
    stepper2_bind.tx_mutex = PcMutexHandle;
    stepper2_bind.driver_addr = TASK_INIT_STEPPER_ADDR_2;
    (void)mod_stepper_ctx_init(stepper2_ctx, &stepper2_bind);
}

/**
 * @brief Bind OLED to the generated I2C handle and perform its one-shot init.
 */
static void task_init_oled_bind(void)
{
    (void)OLED_BindI2C(&hi2c2, OLED_I2C_ADDR_DEFAULT, OLED_I2C_TIMEOUT_MS_DEFAULT);
    OLED_Init();
    OLED_Clear();
    OLED_Update();
}

/**
 * @brief Centralized board assembly hook for this PCB revision.
 * @details
 * Order is explicit: inject maps first, init modules second, bind communication last.
 */
static void task_init_platform_setup(void)
{
    /* Step 1: inject board mapping into default contexts. */
    task_init_bind_map();

    /* Step 2: initialize basic modules and write safe startup state. */
    task_init_modules();

    /* Step 3: reset UART driver dispatch state before protocol modules claim ports. */
    drv_uart_init();

    /* Step 4: bind special peripherals and communication channels. */
    task_init_oled_bind();
    task_init_vofa_bind();
    task_init_vision_module();
    task_init_k230_bind();
    task_init_stepper_bind();
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

    /* One-shot startup assembly: bind, init, write safe states, then open the gate. */
    task_init_platform_setup();

    if (Sem_InitHandle != NULL)
    {
        (void)osSemaphoreRelease(Sem_InitHandle);
    }

    (void)osThreadTerminate(osThreadGetId());
}
