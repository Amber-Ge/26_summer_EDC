/**
 * @file    task_gpio.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   GPIO 反馈任务实现。
 * @details
 * 1. 根据 DCC 运行状态输出灯效、蜂鸣器和激光继电器控制。
 * 2. 消费 KeyTask 的反馈信号量，实现黄灯短闪与按键短鸣。
 * 3. 统一蜂鸣器仲裁：最终输出 = STOP 节奏蜂鸣 OR 按键短鸣。
 */

#include "task_gpio.h"

#include "task_dcc.h"
#include "task_init.h"

#include <stdint.h>

/**
 * @brief 将毫秒换算为 RTOS tick（向上取整，最小 1 tick）。
 */
static uint32_t task_gpio_ms_to_ticks(uint32_t duration_ms, uint32_t tick_freq)
{
    uint32_t ticks;

    if (tick_freq == 0U)
    {
        tick_freq = 1000U;
    }

    ticks = (uint32_t)(((uint64_t)duration_ms * (uint64_t)tick_freq + 999ULL) / 1000ULL);
    if (ticks == 0U)
    {
        ticks = 1U;
    }

    return ticks;
}

/**
 * @brief 判断当前 tick 是否已达到目标 tick（支持回绕）。
 */
static int task_gpio_time_reached(uint32_t now_tick, uint32_t target_tick)
{
    return ((int32_t)(now_tick - target_tick) >= 0);
}

/**
 * @brief 关闭状态灯（红灯与绿灯）。
 */
static void task_gpio_outputs_off(mod_led_ctx_t *led_ctx)
{
    mod_led_off(led_ctx, LED_RED);
    mod_led_off(led_ctx, LED_GREEN);
}

/**
 * @brief GPIO 反馈任务主循环。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartGpioTask(void *argument)
{
    uint32_t tick_freq;            /* RTOS tick 频率 */
    uint32_t key_flash_ticks;      /* 黄灯短闪窗口（tick） */
    uint32_t key_beep_ticks;       /* 按键短鸣窗口（tick） */
    uint32_t green_blink_ticks;    /* ON 态绿灯闪烁周期（tick） */
    uint32_t red_blink_ticks;      /* STOP 态红灯闪烁周期（tick） */
    uint32_t buzzer_on_ticks;      /* STOP 态蜂鸣开窗口（tick） */
    uint32_t buzzer_off_ticks;     /* STOP 态蜂鸣关窗口（tick） */

    uint32_t now_tick;             /* 当前系统 tick */
    uint32_t key_flash_deadline = 0U; /* 黄灯短闪截止 tick */
    uint32_t key_beep_deadline = 0U;  /* 按键短鸣截止 tick */
    uint32_t green_toggle_tick = 0U;  /* 绿灯下一次翻转 tick */
    uint32_t red_toggle_tick = 0U;    /* 红灯下一次翻转 tick */
    uint32_t buzzer_switch_tick = 0U; /* 蜂鸣器下一次翻转 tick */

    uint8_t run_state;             /* DCC 当前运行状态 */

    int key_flash_active = 0;      /* 黄灯短闪窗口是否激活 */
    int key_beep_active = 0;       /* 按键短鸣窗口是否激活 */
    int green_led_on = 0;          /* 绿灯当前输出状态 */
    int red_led_on = 0;            /* 红灯当前输出状态 */
    int buzzer_on = 0;             /* STOP 节奏蜂鸣状态 */
    int buzzer_output_on = 0;      /* 继电器蜂鸣器最终输出状态 */
    int laser_output_on = 0;       /* 激光继电器当前输出状态 */

    mod_led_ctx_t *led_ctx = mod_led_get_default_ctx();         /* LED 模块默认上下文 */
    mod_relay_ctx_t *relay_ctx = mod_relay_get_default_ctx();   /* Relay 模块默认上下文 */

    (void)argument;

    /* 等待 InitTask 完成映射装配与模块初始化。 */
    task_wait_init_done();

#if (TASK_GPIO_STARTUP_ENABLE == 0U)
    /* 启动开关关闭：挂起当前任务，避免输出链路被执行。 */
    (void)osThreadSuspend(osThreadGetId());
    for (;;)
    {
        osDelay(osWaitForever);
    }
#endif

    tick_freq = osKernelGetTickFreq();
    key_flash_ticks = task_gpio_ms_to_ticks(TASK_GPIO_KEY_FLASH_MS, tick_freq);
    key_beep_ticks = task_gpio_ms_to_ticks(TASK_GPIO_KEY_BEEP_MS, tick_freq);
    green_blink_ticks = task_gpio_ms_to_ticks(TASK_GPIO_ON_GREEN_BLINK_MS, tick_freq);
    red_blink_ticks = task_gpio_ms_to_ticks(TASK_GPIO_STOP_RED_BLINK_MS, tick_freq);
    buzzer_on_ticks = task_gpio_ms_to_ticks(TASK_GPIO_STOP_BUZZER_ON_MS, tick_freq);
    buzzer_off_ticks = task_gpio_ms_to_ticks(TASK_GPIO_STOP_BUZZER_OFF_MS, tick_freq);

    /* 上电默认输出安全态。 */
    task_gpio_outputs_off(led_ctx);
    mod_led_off(led_ctx, LED_YELLOW);
    mod_relay_off(relay_ctx, RELAY_BUZZER);
    mod_relay_off(relay_ctx, RELAY_LASER);

    for (;;)
    {
        now_tick = osKernelGetTickCount();

        /* 1) 消费按键反馈事件：启动黄灯脉冲与短鸣窗口。 */
        if (osSemaphoreAcquire(Sem_RedLEDHandle, 0U) == osOK)
        {
            key_flash_active = 1;
            key_flash_deadline = now_tick + key_flash_ticks;
            mod_led_on(led_ctx, LED_YELLOW);

            key_beep_active = 1;
            key_beep_deadline = now_tick + key_beep_ticks;
        }

        /* 2) 激光继电器仅在 DCC ON 态吸合。 */
        run_state = task_dcc_get_run_state();
        if ((run_state == TASK_DCC_RUN_ON) && (laser_output_on == 0))
        {
            laser_output_on = 1;
            mod_relay_on(relay_ctx, RELAY_LASER);
        }
        else if ((run_state != TASK_DCC_RUN_ON) && (laser_output_on != 0))
        {
            laser_output_on = 0;
            mod_relay_off(relay_ctx, RELAY_LASER);
        }

        /* 3) 按 DCC 运行态驱动状态灯和 STOP 蜂鸣节奏。 */
        if (run_state == TASK_DCC_RUN_ON)
        {
            if (red_led_on != 0)
            {
                red_led_on = 0;
                mod_led_off(led_ctx, LED_RED);
            }
            if (buzzer_on != 0)
            {
                buzzer_on = 0;
            }

            if (green_toggle_tick == 0U)
            {
                green_toggle_tick = now_tick + green_blink_ticks;
            }
            if (task_gpio_time_reached(now_tick, green_toggle_tick))
            {
                green_led_on = !green_led_on;
                if (green_led_on != 0)
                {
                    mod_led_on(led_ctx, LED_GREEN);
                }
                else
                {
                    mod_led_off(led_ctx, LED_GREEN);
                }

                green_toggle_tick += green_blink_ticks;
                while (task_gpio_time_reached(now_tick, green_toggle_tick))
                {
                    green_toggle_tick += green_blink_ticks;
                }
            }

            red_toggle_tick = 0U;
            buzzer_switch_tick = 0U;
        }
        else if (run_state == TASK_DCC_RUN_STOP)
        {
            if (green_led_on != 0)
            {
                green_led_on = 0;
                mod_led_off(led_ctx, LED_GREEN);
            }

            if (red_toggle_tick == 0U)
            {
                red_toggle_tick = now_tick + red_blink_ticks;
            }
            if (task_gpio_time_reached(now_tick, red_toggle_tick))
            {
                red_led_on = !red_led_on;
                if (red_led_on != 0)
                {
                    mod_led_on(led_ctx, LED_RED);
                }
                else
                {
                    mod_led_off(led_ctx, LED_RED);
                }

                red_toggle_tick += red_blink_ticks;
                while (task_gpio_time_reached(now_tick, red_toggle_tick))
                {
                    red_toggle_tick += red_blink_ticks;
                }
            }

            if (buzzer_switch_tick == 0U)
            {
                buzzer_on = 1;
                buzzer_switch_tick = now_tick + buzzer_on_ticks;
            }
            else if (task_gpio_time_reached(now_tick, buzzer_switch_tick))
            {
                buzzer_on = !buzzer_on;
                if (buzzer_on != 0)
                {
                    buzzer_switch_tick += buzzer_on_ticks;
                    while (task_gpio_time_reached(now_tick, buzzer_switch_tick))
                    {
                        buzzer_switch_tick += buzzer_on_ticks;
                    }
                }
                else
                {
                    buzzer_switch_tick += buzzer_off_ticks;
                    while (task_gpio_time_reached(now_tick, buzzer_switch_tick))
                    {
                        buzzer_switch_tick += buzzer_off_ticks;
                    }
                }
            }

            green_toggle_tick = 0U;
        }
        else
        {
            task_gpio_outputs_off(led_ctx);
            green_led_on = 0;
            red_led_on = 0;
            buzzer_on = 0;
            green_toggle_tick = 0U;
            red_toggle_tick = 0U;
            buzzer_switch_tick = 0U;
        }

        /* 4) 黄灯脉冲到期关闭。 */
        if ((key_flash_active != 0) && task_gpio_time_reached(now_tick, key_flash_deadline))
        {
            key_flash_active = 0;
            mod_led_off(led_ctx, LED_YELLOW);
        }

        /* 5) 按键短鸣窗口到期关闭。 */
        if ((key_beep_active != 0) && task_gpio_time_reached(now_tick, key_beep_deadline))
        {
            key_beep_active = 0;
        }

        /* 6) 蜂鸣器仲裁：STOP 节奏蜂鸣 OR 按键短鸣。 */
        if ((buzzer_output_on == 0) && ((buzzer_on != 0) || (key_beep_active != 0)))
        {
            buzzer_output_on = 1;
            mod_relay_on(relay_ctx, RELAY_BUZZER);
        }
        else if ((buzzer_output_on != 0) && (buzzer_on == 0) && (key_beep_active == 0))
        {
            buzzer_output_on = 0;
            mod_relay_off(relay_ctx, RELAY_BUZZER);
        }

        osDelay(TASK_GPIO_PERIOD_MS);
    }
}

