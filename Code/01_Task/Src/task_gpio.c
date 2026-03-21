#include "task_gpio.h"
#include "task_init.h"

void StartGpioTask(void *argument)
{
    (void)argument;

    task_wait_init_done();

    for (;;)
    {
        (void)osSemaphoreAcquire(Sem_RedLEDHandle, osWaitForever);

        mod_led_on(LED_RED);
        osDelay(TASK_GPIO_LED_PULSE_MS);
        mod_led_off(LED_RED);

        mod_relay_on(RELAY_BUZZER);
        osDelay(TASK_GPIO_BUZZER_BEEP_MS);
        mod_relay_off(RELAY_BUZZER);
    }
}
