#include <stdio.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <led_strip.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "led_control.h"

static const char *TAG = "led_control";
static led_strip_handle_t led_strip = NULL;
static TaskHandle_t cycle_colors_task_handle = NULL;

static const uint8_t colors[7][3] = {
    {255, 0, 0}, {255, 165, 0}, {255, 255, 0}, {0, 255, 0},
    {0, 0, 255}, {75, 0, 130}, {255, 255, 255}};

static void configure_led(void)
{
    if (led_strip != NULL) return;
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1,
    };
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
    led_strip_clear(led_strip);
}

static void fade_to_color(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2)
{
    for (int step = 0; step <= COLOR_STEPS; step++)
    {
        uint8_t r = r1 + (r2 - r1) * step / COLOR_STEPS;
        uint8_t g = g1 + (g2 - g1) * step / COLOR_STEPS;
        uint8_t b = b1 + (b2 - b1) * step / COLOR_STEPS;
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
        vTaskDelay(FADE_DELAY_MS / portTICK_PERIOD_MS);
    }
}

static void cycle_colors(void *arg)
{
    configure_led();
    int color_index = 0;
    while (1)
    {
        int next_color_index = (color_index + 1) % 7;
        fade_to_color(colors[color_index][0], colors[color_index][1], colors[color_index][2],
                      colors[next_color_index][0], colors[next_color_index][1], colors[next_color_index][2]);
        color_index = next_color_index;
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void led_control_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

void turn_on_single_led(void)
{
    ESP_LOGI(TAG, "Switching to SINGLE LED mode...");
    turn_off_rgb();
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT

);
    gpio_set_level(LED_GPIO, 1);
    ESP_LOGI(TAG, "Single LED turned ON");
}

void turn_off_single_led(void)
{
    gpio_set_level(LED_GPIO, 0);
    ESP_LOGI(TAG, "Single LED turned OFF");
}

void turn_on_rgb(void)
{
    turn_off_single_led();
    if (led_strip == NULL) configure_led();
    if (cycle_colors_task_handle == NULL)
    {
        xTaskCreate(&cycle_colors, "cycle_colors_task", 4096, NULL, 5, &cycle_colors_task_handle);
    }
    ESP_LOGI(TAG, "RGB LED turned ON");
}

void turn_off_rgb(void)
{
    ESP_LOGI(TAG, "Turning off RGB LED");
    if (cycle_colors_task_handle != NULL)
    {
        vTaskDelete(cycle_colors_task_handle);
        cycle_colors_task_handle = NULL;
    }
    if (led_strip != NULL)
    {
        led_strip_clear(led_strip);
        ESP_ERROR_CHECK(led_strip_del(led_strip));
        led_strip = NULL;
    }
}