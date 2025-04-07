#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <stdbool.h>

#include "command_handler.h"
#include "led_control.h"
#include "nvs_storage.h"

// Định nghĩa các hằng số
#define TAG "command_handler"                   ///< Tag dùng cho logging
#define DEFAULT_TOPIC "/speech/command"         ///< Topic mặc định cho lệnh điều khiển
#define MAX_DATA_LENGTH 32                      ///< Độ dài tối đa của dữ liệu nhận được

// Định nghĩa các hàm xử lý lệnh private
static void handle_on(void) { turn_on_single_led(); }   ///< Bật LED đơn
static void handle_off(void) { turn_off_single_led(); } ///< Tắt LED đơn
static void handle_on_rgb(void) { turn_on_rgb(); }      ///< Bật LED RGB
static void handle_off_rgb(void) { turn_off_rgb(); }    ///< Tắt LED RGB

/**
 * @brief Bảng ánh xạ lệnh cho các topic không mặc định
 */
static HashEntry command_map[] = {
    {"on", {handle_on}},
    {"off", {handle_off}},
    {"onRGB", {handle_on_rgb}},
    {"offRGB", {handle_off_rgb}},
    {"deleteNVS", {handle_erase_wifi}},
    {"changeWifi", {handle_erase_wifi}},
    {NULL, {NULL}} // Sentinel để kết thúc mảng
};

/**
 * @brief Tìm chiến lược xử lý lệnh dựa trên key
 * @param key Chuỗi lệnh cần tìm
 * @return Con trỏ đến CommandStrategy nếu tìm thấy, NULL nếu không
 */
static CommandStrategy *find_strategy(const char *key)
{
    if (key == NULL) {
        return NULL;
    }

    for (int i = 0; command_map[i].key != NULL; i++) {
        if (strcmp(command_map[i].key, key) == 0) {
            return &command_map[i].strategy;
        }
    }
    return NULL;
}

/**
 * @brief Xử lý lệnh cho topic mặc định (/speech/command)
 * @param command Chuỗi lệnh nhận được
 * @param topic Topic nhận được
 */
static void process_default_topic_command(const char *command, const char *topic)
{
    if (strcmp(command, "turn on") == 0) {
        handle_on();
    } else if (strcmp(command, "turn off") == 0) {
        handle_off();
    } else {
        ESP_LOGW(TAG, "Command '%s' not supported on default topic '%s'", command, topic);
    }
}

/**
 * @brief Xử lý lệnh cho topic không mặc định
 * @param command Chuỗi lệnh nhận được
 * @param topic Topic nhận được
 */
static void process_non_default_topic_command(const char *command, const char *topic)
{
    CommandStrategy *strategy = find_strategy(command);
    if (strategy != NULL && strategy->handle != NULL) {
        strategy->handle();
    } else {
        ESP_LOGW(TAG, "Unknown command '%s' from topic '%s'", command, topic);
    }
/**
 * @brief Xử lý lệnh LED dựa trên topic và dữ liệu nhận được
 * @param topic Chuỗi topic nhận được
 * @param data Chuỗi dữ liệu chứa lệnh
 * @param len Độ dài của dữ liệu
 * @param dynamic_topic Chuỗi topic động được tạo từ địa chỉ MAC
 */
void handle_led_command(const char *topic, const char *data, int len, const char *dynamic_topic)
{
    // Kiểm tra đầu vào
    if (topic == NULL || data == NULL || len <= 0 || dynamic_topic == NULL) {
        ESP_LOGE(TAG, "Invalid input: topic=%p, data=%p, len=%d, dynamic_topic=%p", topic, data, len, dynamic_topic);
        return;
    }
    
    // Chuẩn bị buffer cho dữ liệu nhận được
    char received_data[MAX_DATA_LENGTH] = {0};
    int safe_len = (len >= MAX_DATA_LENGTH) ? MAX_DATA_LENGTH - 1 : len;
    strncpy(received_data, data, safe_len);
    received_data[safe_len] = '\0';

    // Kiểm tra topic mặc định
    bool is_default_topic = (strcmp(topic, DEFAULT_TOPIC) == 0);

    // Ghi log thông tin nhận được, bao gồm topic động
    ESP_LOGI(TAG, "Received from topic '%s' - command: '%s' (Default topic: %s), Dynamic topic: '%s'",
             topic, received_data, is_default_topic ? "Yes" : "No", dynamic_topic);
    
    // Xử lý lệnh theo loại topic
    if (is_default_topic) {
        process_default_topic_command(received_data, topic);
    } else if(strcmp(topic, dynamic_topic) == 0) {
        process_non_default_topic_command(received_data, topic);
    }
}

/**
 * @brief Xóa thông tin WiFi đã lưu trong NVS
 */
void handle_erase_wifi(void)
{
    erase_wifi_credentials();
}