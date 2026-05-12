/**
 * @file    task_debug.c
 * @brief   Debug task: heartbeat + PID tuning telemetry.
 */

#include "task_debug.h"

#include "cmsis_os.h"
#include "stm32f4xx_hal.h"

#include "task_chassis.h"
#include "task_init.h"

#include "mod_led.h"
#include "mod_vofa.h"

#define TASK_DEBUG_DEFAULT_MODE            (TASK_DEBUG_MODE_NONE)
#define TASK_DEBUG_LOOP_DELAY_MS           (10U)
#define TASK_DEBUG_LED_BROAD_TOGGLE_MS     (200U)
static volatile task_debug_mode_t s_debug_mode = TASK_DEBUG_DEFAULT_MODE;
static uint32_t s_debug_vofa_send_ok = 0U;
static uint32_t s_debug_vofa_send_fail = 0U;

static void task_debug_run_pid_tune_uart(void)
{
    task_chassis_telemetry_t telemetry;
    int32_t payload[4];
    const char *tag = "motor_pid";
    bool send_ok;

    if (!task_chassis_get_telemetry(&telemetry))
    {
        return;
    }

    if (telemetry.mode == TASK_CHASSIS_CMD_MODE_PWM_DEBUG)
    {
        payload[0] = telemetry.left_duty_cmd;
        payload[1] = telemetry.right_duty_cmd;
        payload[2] = telemetry.left_actual_speed;
        payload[3] = telemetry.right_actual_speed;
        tag = "motor_pwm";
    }
    else
    {
        payload[0] = telemetry.left_target_speed;
        payload[1] = telemetry.right_target_speed;
        payload[2] = telemetry.left_actual_speed;
        payload[3] = telemetry.right_actual_speed;
    }

    send_ok = mod_vofa_send_int(tag, payload, 4U);
    if (send_ok)
    {
        s_debug_vofa_send_ok++;
    }
    else
    {
        s_debug_vofa_send_fail++;
    }
}

bool task_debug_set_mode(task_debug_mode_t mode)
{
    if (mode >= TASK_DEBUG_MODE_MAX)
    {
        return false;
    }

    s_debug_mode = mode;
    return true;
}

task_debug_mode_t task_debug_get_mode(void)
{
    return s_debug_mode;
}

void StartDebugTask(void *argument)
{
    mod_led_ctx_t *led_ctx = mod_led_get_default_ctx();
    uint32_t last_led_toggle_tick = 0U;

    (void)argument;

    task_wait_init_done();
    last_led_toggle_tick = HAL_GetTick();

    for (;;)
    {
        uint32_t now_tick = HAL_GetTick();

        if ((now_tick - last_led_toggle_tick) >= TASK_DEBUG_LED_BROAD_TOGGLE_MS)
        {
            mod_led_toggle(led_ctx, LED_BROAD);
            last_led_toggle_tick = now_tick;
        }

        switch (task_debug_get_mode())
        {
        case TASK_DEBUG_MODE_PID_TUNE_UART:
            task_debug_run_pid_tune_uart();
            break;
        case TASK_DEBUG_MODE_NONE:
        default:
            break;
        }

        osDelay(TASK_DEBUG_LOOP_DELAY_MS);
    }
}
