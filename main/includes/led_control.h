#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <esp_err.h>

#define LED_GPIO GPIO_NUM_48
#define BLINK_GPIO 48
#define FADE_DELAY_MS 20
#define COLOR_STEPS 50

void led_control_init(void);
void turn_on_single_led(void);
void turn_off_single_led(void);
void turn_on_rgb(void);
void turn_off_rgb(void);

#endif