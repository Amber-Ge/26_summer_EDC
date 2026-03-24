/**
 * @file    task_gpio.c
 * @brief   GPIO 任务实现。
 * @details
 * 1. 文件作用：实现 GPIO 相关任务逻辑与外设状态控制。
 * 2. 上下层绑定：上层由任务调度层触发；下层调用 LED/继电器等模块接口。
 */
#include "task_gpio.h"
#include "task_dcc.h"
#include "task_init.h"
#include <stdint.h>

/* 毫秒转换为tick，向上取整且最小1 tick */
/**
 * @brief 将毫秒时间换算为 RTOS tick，结果向上取整且至少为 1 tick。
 * @param duration_ms 目标时长（毫秒）。
 * @param tick_freq RTOS tick 频率（Hz），传入 0 时按 1000Hz 兜底。
 * @return 换算后的 tick 数。
 */
static uint32_t task_gpio_ms_to_ticks(uint32_t duration_ms, uint32_t tick_freq)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint32_t ticks; // 换算后的 tick 计数

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

/* Tick比较：now是否已经到达target（支持回绕） */
/**
 * @brief 判断当前 tick 是否达到目标 tick（支持计数回绕）。
 * @param now_tick 当前内核 tick。
 * @param target_tick 目标触发 tick。
 * @return 达到/超过目标返回 1，否则返回 0。
 */
static int task_gpio_time_reached(uint32_t now_tick, uint32_t target_tick)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    return ((int32_t)(now_tick - target_tick) >= 0);
}

/* OFF/PREPARE统一清零输出，防止状态残留 */
/**
 * @brief 关闭 GPIO 任务管理的状态灯输出。
 * @param 无。
 * @return 无。
 */
static void task_gpio_outputs_off(void)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    mod_led_off(LED_RED);
    mod_led_off(LED_GREEN);
}

/**
 * @brief GPIO 任务主循环：处理按键反馈灯效、运行态灯效、蜂鸣器与激光继电器。
 * @param argument 任务参数（未使用）。
 * @return 无。
 */
void StartGpioTask(void *argument)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    uint32_t tick_freq;                 /* RTOS tick 频率 */
    uint32_t key_flash_ticks;           /* 黄灯短闪持续 tick */
    uint32_t key_beep_ticks;            /* 按键短鸣持续 tick */
    uint32_t green_blink_ticks;         /* ON 态绿灯闪烁周期 tick */
    uint32_t red_blink_ticks;           /* STOP 态红灯闪烁周期 tick */
    uint32_t buzzer_on_ticks;           /* STOP 态蜂鸣器响铃时长 tick */
    uint32_t buzzer_off_ticks;          /* STOP 态蜂鸣器静默时长 tick */

    uint32_t now_tick;                  /* 当前系统 tick */
    uint32_t key_flash_deadline = 0U;   /* 黄灯关闭时刻 */
    uint32_t key_beep_deadline = 0U;    /* 按键短鸣结束时刻 */
    uint32_t green_toggle_tick = 0U;    /* 绿灯下次翻转时刻 */
    uint32_t red_toggle_tick = 0U;      /* 红灯下次翻转时刻 */
    uint32_t buzzer_switch_tick = 0U;   /* 蜂鸣器下次相位切换时刻 */

    uint8_t run_state;                  /* DCC 当前运行状态 */
    int key_flash_active = 0;           /* 黄灯短闪活动标志 */
    int key_beep_active = 0;            /* 按键短鸣活动标志 */
    int green_led_on = 0;               /* 绿灯逻辑输出状态 */
    int red_led_on = 0;                 /* 红灯逻辑输出状态 */
    int buzzer_on = 0;                  /* STOP 态蜂鸣节奏状态 */
    int buzzer_output_on = 0;           /* 蜂鸣器继电器当前输出状态 */
    int laser_output_on = 0;            /* 激光继电器当前输出状态 */

    (void)argument;

    task_wait_init_done();

    tick_freq = osKernelGetTickFreq();
    key_flash_ticks = task_gpio_ms_to_ticks(TASK_GPIO_KEY_FLASH_MS, tick_freq);
    key_beep_ticks = task_gpio_ms_to_ticks(TASK_GPIO_KEY_BEEP_MS, tick_freq);
    green_blink_ticks = task_gpio_ms_to_ticks(TASK_GPIO_ON_GREEN_BLINK_MS, tick_freq);
    red_blink_ticks = task_gpio_ms_to_ticks(TASK_GPIO_STOP_RED_BLINK_MS, tick_freq);
    buzzer_on_ticks = task_gpio_ms_to_ticks(TASK_GPIO_STOP_BUZZER_ON_MS, tick_freq);
    buzzer_off_ticks = task_gpio_ms_to_ticks(TASK_GPIO_STOP_BUZZER_OFF_MS, tick_freq);

    task_gpio_outputs_off();
    mod_led_off(LED_YELLOW);
    mod_relay_off(RELAY_BUZZER);
    mod_relay_off(RELAY_LASER);

    for (;;) // 循环计数器
    {
        now_tick = osKernelGetTickCount();

        /* 任意按键事件：黄灯短闪 */
        if (osSemaphoreAcquire(Sem_RedLEDHandle, 0U) == osOK)
        {
            key_flash_active = 1;
            key_flash_deadline = now_tick + key_flash_ticks;
            mod_led_on(LED_YELLOW);

            key_beep_active = 1;
            key_beep_deadline = now_tick + key_beep_ticks;
        }

        run_state = task_dcc_get_run_state();
        if ((run_state == TASK_DCC_RUN_ON) && (laser_output_on == 0))
        {
            laser_output_on = 1;
            mod_relay_on(RELAY_LASER);
        }
        else if ((run_state != TASK_DCC_RUN_ON) && (laser_output_on != 0))
        {
            laser_output_on = 0;
            mod_relay_off(RELAY_LASER);
        }

        if (run_state == TASK_DCC_RUN_ON)
        {
            /* ON态：绿灯持续闪烁，红灯/蜂鸣关闭 */
            if (red_led_on != 0)
            {
                red_led_on = 0;
                mod_led_off(LED_RED);
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
                    mod_led_on(LED_GREEN);
                }
                else
                {
                    mod_led_off(LED_GREEN);
                }

                green_toggle_tick += green_blink_ticks;
                while (task_gpio_time_reached(now_tick, green_toggle_tick))
                {
                    green_toggle_tick += green_blink_ticks;
                }
            }

            /* 离开STOP后，重置STOP节奏基准 */
            red_toggle_tick = 0U;
            buzzer_switch_tick = 0U;
        }
        else if (run_state == TASK_DCC_RUN_STOP)
        {
            /* STOP态：红灯闪烁 + 蜂鸣器短响循环，绿灯关闭 */
            if (green_led_on != 0)
            {
                green_led_on = 0;
                mod_led_off(LED_GREEN);
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
                    mod_led_on(LED_RED);
                }
                else
                {
                    mod_led_off(LED_RED);
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

            /* 离开ON后，重置ON节奏基准 */
            green_toggle_tick = 0U;
        }
        else
        {
            /* OFF/PREPARE：不输出绿灯/红灯/蜂鸣 */
            task_gpio_outputs_off();
            green_led_on = 0;
            red_led_on = 0;
            buzzer_on = 0;
            green_toggle_tick = 0U;
            red_toggle_tick = 0U;
            buzzer_switch_tick = 0U;
        }

        /* 黄灯脉冲超时关闭 */
        if ((key_flash_active != 0) && task_gpio_time_reached(now_tick, key_flash_deadline))
        {
            key_flash_active = 0;
            mod_led_off(LED_YELLOW);
        }

        /* 按键蜂鸣超时关闭 */
        if ((key_beep_active != 0) && task_gpio_time_reached(now_tick, key_beep_deadline))
        {
            key_beep_active = 0;
        }

        /* 蜂鸣器输出 = STOP蜂鸣节奏 OR 按键短鸣 */
        if ((buzzer_output_on == 0) && ((buzzer_on != 0) || (key_beep_active != 0)))
        {
            buzzer_output_on = 1;
            mod_relay_on(RELAY_BUZZER);
        }
        else if ((buzzer_output_on != 0) && (buzzer_on == 0) && (key_beep_active == 0))
        {
            buzzer_output_on = 0;
            mod_relay_off(RELAY_BUZZER);
        }

        osDelay(TASK_GPIO_PERIOD_MS);
    }
}

