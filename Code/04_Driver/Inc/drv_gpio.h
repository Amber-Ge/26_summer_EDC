#ifndef FINAL_GRADUATE_WORK_DRV_GPIO_H
#define FINAL_GRADUATE_WORK_DRV_GPIO_H

#include "main.h"

typedef enum
{
    GPIO_LEVEL_LOW = 0,
    GPIO_LEVEL_HIGH = 1
} gpio_level_e;

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} drv_gpio_pin_t;

void drv_gpio_write(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, gpio_level_e level);
void drv_gpio_write_pin(const drv_gpio_pin_t *pin, gpio_level_e level);

gpio_level_e drv_gpio_read(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
gpio_level_e drv_gpio_read_pin(const drv_gpio_pin_t *pin);

void drv_gpio_toggle(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void drv_gpio_toggle_pin(const drv_gpio_pin_t *pin);

#endif /* FINAL_GRADUATE_WORK_DRV_GPIO_H */
