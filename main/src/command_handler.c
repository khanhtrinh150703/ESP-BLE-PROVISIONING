#include <stdio.h>
#include <string.h>
#include <esp_log.h>

#include "command_handler.h"
#include "led_control.h"
#include "nvs_storage.h"

static const char *TAG = "command_handler";

// Định nghĩa các hàm private
static void handle_on(void) { turn_on_single_led(); }
static void handle_off(void) { turn_off_single_led(); }
static void handle_on_rgb(void) { turn_on_rgb(); }
static void handle_off_rgb(void) { turn_off_rgb(); }

// Định nghĩa bảng command_map
static HashEntry command_map[] = {
    {"on", {handle_on}},
    {"off", {handle_off}},
    {"onRGB", {handle_on_rgb}},
    {"offRGB", {handle_off_rgb}},
    {"deleteNVS", {handle_erase_wifi}},
    {"changeWifi", {handle_erase_wifi}},
    {NULL, {NULL}} // Kết thúc mảng
};

// Định nghĩa hàm find_strategy
static CommandStrategy *find_strategy(const char *key)
{
    for (int i = 0; command_map[i].key != NULL; i++)
    {
        if (strcmp(command_map[i].key, key) == 0)
        {
            return &command_map[i].strategy;
        }
    }
    return NULL;
}

// Định nghĩa hàm public
void handle_led_command(const char *data, int len)
{
    char received_data[32] = {0};
    if (len >= sizeof(received_data)) len = sizeof(received_data) - 1;
    strncpy(received_data, data, len);
    received_data[len] = '\0';

    ESP_LOGI(TAG, "Received command: %s", received_data);
    CommandStrategy *strategy = find_strategy(received_data);
    if (strategy != NULL && strategy->handle != NULL)
    {
        strategy->handle();
    }
    else
    {
        ESP_LOGW(TAG, "Unknown command: %s", received_data);
    }
}

void handle_erase_wifi(void)
{
    erase_wifi_credentials();
}