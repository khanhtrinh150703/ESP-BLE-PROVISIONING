#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_system.h>
#include <esp_netif.h>
#include <nvs_flash.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#include <lwip/sockets.h>
#include <lwip/dns.h>
#include <lwip/netdb.h>

#include <mqtt_client.h>
#include <driver/gpio.h>
#include <led_strip.h>

#include "qrcode.h"

static const char *TAG = "app";

#define EXAMPLE_PROV_SEC2_USERNAME "wifiprov"
#define EXAMPLE_PROV_SEC2_PWD "abcd1234"
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64
#define CONFIG_BLINK_LED_STRIP_BACKEND_SPI 1
#define ESP_BROKER_IP "mqtt://192.168.1.8:1883"
#define LED_GPIO GPIO_NUM_48 // Chân GPIO để điều khiển LED
#define BLINK_GPIO 48
#define FADE_DELAY_MS 20 // Độ trễ giữa các bước chuyển màu
#define COLOR_STEPS 50   // Số bước chuyển đổi giữa các màu

static led_strip_handle_t led_strip = NULL;
static uint32_t MQTT_CONNECTED = 0;
static esp_mqtt_client_handle_t client = NULL;
static TaskHandle_t cycle_colors_task_handle = NULL; // Biến lưu task handle

void handle_led_command(const char *data, int len);
void cycle_colors();
void turn_off_rgb();
void turn_on_rgb();
void turn_on_single_led();
void turn_off_single_led();
void reset_gpio_for_single_led();
static void mqtt_app_start(void);
void publish_device_status(const char *status);

const uint8_t colors[7][3] = {
    {255, 0, 0},    // Đỏ
    {255, 165, 0},  // Cam
    {255, 255, 0},  // Vàng
    {0, 255, 0},    // Xanh lá
    {0, 0, 255},    // Xanh dương
    {75, 0, 130},   // Chàm
    {255, 255, 255} // Trắng
};

void fade_to_color(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2)
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

static void configure_led(void)
{
    if (led_strip != NULL)
    {
        ESP_LOGI(TAG, "LED strip already configured. Skipping initialization.");
        return; // Tránh khởi tạo lại nếu đã có
    }

    ESP_LOGI(TAG, "Configuring addressable LED!");
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1,
    };

#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#else
#error "unsupported LED strip backend"
#endif

    if (led_strip != NULL)
    {
        led_strip_clear(led_strip); // Chỉ gọi nếu đã được khởi tạo thành công
    }
    else
    {
        ESP_LOGE(TAG, "LED strip initialization failed!");
    }
}

static char stored_ssid[MAX_SSID_LEN] = {0};     // Lưu SSID
static char stored_password[MAX_PASS_LEN] = {0}; // Lưu Password

static const char sec2_salt[] = {
    0x03, 0x6e, 0xe0, 0xc7, 0xbc, 0xb9, 0xed, 0xa8,
    0x4c, 0x9e, 0xac, 0x97, 0xd9, 0x3d, 0xec, 0xf4};
static const char sec2_verifier[] = {
    0x7c, 0x7c, 0x85, 0x47, 0x65, 0x08, 0x94, 0x6d, 0xd6, 0x36, 0xaf, 0x37, 0xd7, 0xe8, 0x91, 0x43,
    0x78, 0xcf, 0xfd, 0x61, 0x6c, 0x59, 0xd2, 0xf8, 0x39, 0x08, 0x12, 0x72, 0x38, 0xde, 0x9e, 0x24,
    0xa4, 0x70, 0x26, 0x1c, 0xdf, 0xa9, 0x03, 0xc2, 0xb2, 0x70, 0xe7, 0xb1, 0x32, 0x24, 0xda, 0x11,
    0x1d, 0x97, 0x18, 0xdc, 0x60, 0x72, 0x08, 0xcc, 0x9a, 0xc9, 0x0c, 0x48, 0x27, 0xe2, 0xae, 0x89,
    0xaa, 0x16, 0x25, 0xb8, 0x04, 0xd2, 0x1a, 0x9b, 0x3a, 0x8f, 0x37, 0xf6, 0xe4, 0x3a, 0x71, 0x2e,
    0xe1, 0x27, 0x86, 0x6e, 0xad, 0xce, 0x28, 0xff, 0x54, 0x46, 0x60, 0x1f, 0xb9, 0x96, 0x87, 0xdc,
    0x57, 0x40, 0xa7, 0xd4, 0x6c, 0xc9, 0x77, 0x54, 0xdc, 0x16, 0x82, 0xf0, 0xed, 0x35, 0x6a, 0xc4,
    0x70, 0xad, 0x3d, 0x90, 0xb5, 0x81, 0x94, 0x70, 0xd7, 0xbc, 0x65, 0xb2, 0xd5, 0x18, 0xe0, 0x2e,
    0xc3, 0xa5, 0xf9, 0x68, 0xdd, 0x64, 0x7b, 0xb8, 0xb7, 0x3c, 0x9c, 0xfc, 0x00, 0xd8, 0x71, 0x7e,
    0xb7, 0x9a, 0x7c, 0xb1, 0xb7, 0xc2, 0xc3, 0x18, 0x34, 0x29, 0x32, 0x43, 0x3e, 0x00, 0x99, 0xe9,
    0x82, 0x94, 0xe3, 0xd8, 0x2a, 0xb0, 0x96, 0x29, 0xb7, 0xdf, 0x0e, 0x5f, 0x08, 0x33, 0x40, 0x76,
    0x52, 0x91, 0x32, 0x00, 0x9f, 0x97, 0x2c, 0x89, 0x6c, 0x39, 0x1e, 0xc8, 0x28, 0x05, 0x44, 0x17,
    0x3f, 0x68, 0x02, 0x8a, 0x9f, 0x44, 0x61, 0xd1, 0xf5, 0xa1, 0x7e, 0x5a, 0x70, 0xd2, 0xc7, 0x23,
    0x81, 0xcb, 0x38, 0x68, 0xe4, 0x2c, 0x20, 0xbc, 0x40, 0x57, 0x76, 0x17, 0xbd, 0x08, 0xb8, 0x96,
    0xbc, 0x26, 0xeb, 0x32, 0x46, 0x69, 0x35, 0x05, 0x8c, 0x15, 0x70, 0xd9, 0x1b, 0xe9, 0xbe, 0xcc,
    0xa9, 0x38, 0xa6, 0x67, 0xf0, 0xad, 0x50, 0x13, 0x19, 0x72, 0x64, 0xbf, 0x52, 0xc2, 0x34, 0xe2,
    0x1b, 0x11, 0x79, 0x74, 0x72, 0xbd, 0x34, 0x5b, 0xb1, 0xe2, 0xfd, 0x66, 0x73, 0xfe, 0x71, 0x64,
    0x74, 0xd0, 0x4e, 0xbc, 0x51, 0x24, 0x19, 0x40, 0x87, 0x0e, 0x92, 0x40, 0xe6, 0x21, 0xe7, 0x2d,
    0x4e, 0x37, 0x76, 0x2f, 0x2e, 0xe2, 0x68, 0xc7, 0x89, 0xe8, 0x32, 0x13, 0x42, 0x06, 0x84, 0x84,
    0x53, 0x4a, 0xb3, 0x0c, 0x1b, 0x4c, 0x8d, 0x1c, 0x51, 0x97, 0x19, 0xab, 0xae, 0x77, 0xff, 0xdb,
    0xec, 0xf0, 0x10, 0x95, 0x34, 0x33, 0x6b, 0xcb, 0x3e, 0x84, 0x0f, 0xb9, 0xd8, 0x5f, 0xb8, 0xa0,
    0xb8, 0x55, 0x53, 0x3e, 0x70, 0xf7, 0x18, 0xf5, 0xce, 0x7b, 0x4e, 0xbf, 0x27, 0xce, 0xce, 0xa8,
    0xb3, 0xbe, 0x40, 0xc5, 0xc5, 0x32, 0x29, 0x3e, 0x71, 0x64, 0x9e, 0xde, 0x8c, 0xf6, 0x75, 0xa1,
    0xe6, 0xf6, 0x53, 0xc8, 0x31, 0xa8, 0x78, 0xde, 0x50, 0x40, 0xf7, 0x62, 0xde, 0x36, 0xb2, 0xba};

static esp_err_t example_get_sec2_salt(const char **salt, uint16_t *salt_len)
{
    ESP_LOGI(TAG, "Development mode: using hard coded salt");
    *salt = sec2_salt;
    *salt_len = sizeof(sec2_salt);
    return ESP_OK;
}

static esp_err_t example_get_sec2_verifier(const char **verifier, uint16_t *verifier_len)
{
    ESP_LOGI(TAG, "Development mode: using hard coded verifier");
    *verifier = sec2_verifier;
    *verifier_len = sizeof(sec2_verifier);
    return ESP_OK;
}

const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;
#include <nvs.h>

// Hàm lưu thông tin Wi-Fi vào NVS
static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Wi-Fi credentials saved to NVS: SSID=%s, Password=%s", ssid, password);
    return err;
}

// Hàm đọc thông tin Wi-Fi từ NVS
static esp_err_t load_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "NVS not found or empty: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, "password", password, &pass_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Wi-Fi credentials loaded from NVS: SSID=%s, Password=%s", ssid, password);
    return ESP_OK;
}

#include <nvs_flash.h>

#include <nvs.h>

static esp_err_t erase_wifi_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Mở namespace "wifi_config" để đọc/ghi
    err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Xóa key "ssid"
    err = nvs_erase_key(nvs_handle, "ssid");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "Error erasing SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Xóa key "password"
    err = nvs_erase_key(nvs_handle, "password");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "Error erasing password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit thay đổi
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Wi-Fi credentials erased from NVS");
    return ESP_OK;
}

#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_SOFTAP "softap"
#define PROV_TRANSPORT_BLE "ble"
#define QRCODE_BASE_URL "https://espressif.github.io/esp-jumpstart/qrcode.html"
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    static int retries;

    if (event_base == WIFI_PROV_EVENT)
    {
        switch (event_id)
        {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;

        case WIFI_PROV_CRED_RECV:
        {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            strncpy(stored_ssid, (const char *)wifi_sta_cfg->ssid, MAX_SSID_LEN - 1);
            strncpy(stored_password, (const char *)wifi_sta_cfg->password, MAX_PASS_LEN - 1);
            stored_ssid[MAX_SSID_LEN - 1] = '\0';
            stored_password[MAX_PASS_LEN - 1] = '\0';
            ESP_LOGI(TAG, "Stored Wi-Fi Credentials: SSID: %s, Password: %s", stored_ssid, stored_password);
            // Lưu thông tin Wi-Fi vào NVS
            save_wifi_credentials(stored_ssid, stored_password);
            // Apply credentials
            wifi_config_t wifi_cfg = {0};
            strncpy((char *)wifi_cfg.sta.ssid, stored_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
            strncpy((char *)wifi_cfg.sta.password, stored_password, sizeof(wifi_cfg.sta.password) - 1);
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
            esp_wifi_connect();
            break;
        }

        case WIFI_PROV_CRED_FAIL:
        {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                          "\n\tPlease reset to factory and retry provisioning",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
            retries++;
            if (retries >= CONFIG_EXAMPLE_PROV_MGR_MAX_RETRY_CNT)
            {
                ESP_LOGI(TAG, "Failed to connect with provisioned AP, resetting provisioned credentials");
                wifi_prov_mgr_reset_sm_state_on_failure();
                retries = 0;
            }
            break;
        }

        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            retries = 0;
            break;

        case WIFI_PROV_END:
            wifi_prov_mgr_deinit();
            break;

        default:
            break;
        }
    }
    else if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
            esp_wifi_connect();
            break;

        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }
    else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT)
    {
        switch (event_id)
        {
        case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
            ESP_LOGI(TAG, "BLE transport: Connected!");
            break;

        case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
            ESP_LOGI(TAG, "BLE transport: Disconnected!");
            break;

        default:
            break;
        }
    }
    else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT)
    {
        switch (event_id)
        {
        case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
            ESP_LOGI(TAG, "Secured session established!");
            break;

        case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
            ESP_LOGE(TAG, "Received invalid security parameters for establishing secure session!");
            break;

        case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
            ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing secure session!");
            break;

        default:
            break;
        }
    }
}

static bool isPublish = false;
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to broker");
        MQTT_CONNECTED = 1;

        // Subscribe vào topic điều khiển của thiết bị
        char command_topic[64];
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(command_topic, sizeof(command_topic), "/devices/esp_device_%02X%02X%02X/command", mac[3], mac[4], mac[5]);
        int msg_id = esp_mqtt_client_subscribe(client, command_topic, 0);
        ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", command_topic, msg_id);

        // Publish topic lên /phone/notification nếu chưa publish
        if (!isPublish)
        {
            int msg_id2 = esp_mqtt_client_publish(client, "/phone/notification", command_topic, 0, 1, 0);
            ESP_LOGI(TAG, "Published command topic to /phone/notification: %s, msg_id=%d", command_topic, msg_id2);
            isPublish = true; // Đặt thành true để không publish lại
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected from broker");
        MQTT_CONNECTED = 0;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error: %s", event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS ? "TLS error" : "Connection error");
        if (event->error_handle->esp_tls_last_esp_err != 0)
        {
            ESP_LOGE(TAG, "ESP-TLS error: %s", esp_err_to_name(event->error_handle->esp_tls_last_esp_err));
        }
        ESP_LOGE(TAG, "Connect return code: %d", event->error_handle->connect_return_code);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data - Topic: %.*s, Data: %.*s", event->topic_len, event->topic, event->data_len, event->data);
        handle_led_command(event->data, event->data_len);
        break;
    default:
        ESP_LOGI(TAG, "Other MQTT event id: %d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "Starting MQTT client...");

    // Lấy địa chỉ MAC để tạo client_id duy nhất
    uint8_t mac[6];
    char client_id[32];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(client_id, sizeof(client_id), "esp_device_%02X%02X%02X", mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t mqttConfig = {
        .broker.address.uri = "mqtt://192.168.1.34",
        .broker.address.port = 1883,
        .credentials.client_id = client_id,
        .session.keepalive = 60,
    };

    client = esp_mqtt_client_init(&mqttConfig);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }

    esp_err_t err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        return;
    }

    err = esp_mqtt_client_start(client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
    }
    // Không subscribe ở đây nữa, đã chuyển vào MQTT_EVENT_CONNECTED
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf)
    {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL)
    {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1;

    return ESP_OK;
}

static void wifi_prov_print_qr(const char *name, const char *username, const char *pop, const char *transport)
{
    if (!name || !transport)
    {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }
    char payload[150] = {0};
    if (pop)
    {
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\""
                                           ",\"username\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, username, pop, transport);
    }
    else
    {
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\""
                                           ",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, transport);
    }

    ESP_LOGI(TAG, "Scan this QR code from the provisioning application for Provisioning.");

    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);

    ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s", QRCODE_BASE_URL, payload);
}

void cycle_colors()
{
    configure_led();
    int color_index = 0;

    while (1)
    {
        ESP_LOGI(TAG, "Fading LED to color index: %d", color_index);
        int next_color_index = (color_index + 1) % 7;

        fade_to_color(colors[color_index][0], colors[color_index][1], colors[color_index][2],
                      colors[next_color_index][0], colors[next_color_index][1], colors[next_color_index][2]);

        color_index = next_color_index;

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void handle_led_command(const char *data, int len)
{
    char received_data[32] = {0};
    strncpy(received_data, data, len < sizeof(received_data) ? len : sizeof(received_data) - 1);
    received_data[sizeof(received_data) - 1] = '\0';

    ESP_LOGI(TAG, "Received command: %s", received_data);

    if (strcmp(received_data, "on") == 0)
    {
        turn_on_single_led();
    }
    else if (strcmp(received_data, "off") == 0)
    {
        turn_off_single_led();
    }
    else if (strcmp(received_data, "onRGB") == 0)
    {
        turn_on_rgb();
    }
    else if (strcmp(received_data, "offRGB") == 0)
    {
        turn_off_rgb();
    }
    else if (strcmp(received_data, "deleteNVS") == 0)
    {
        erase_wifi_credentials();
    }
    else
    {
        ESP_LOGW(TAG, "Unknown command: %s", received_data);
        publish_device_status("error:unknown_command");
    }
}

void publish_device_status(const char *status)
{
    //     if (MQTT_CONNECTED)
    //     {
    //         char status_topic[64];
    //         char client_id[32];
    //         uint8_t mac[6];
    //         esp_wifi_get_mac(WIFI_IF_STA, mac);
    //         snprintf(client_id, sizeof(client_id), "esp_device_%02X%02X%02X", mac[3], mac[4], mac[5]);
    //         snprintf(status_topic, sizeof(status_topic), "/devices/%s/status", client_id);

    //         int msg_id = esp_mqtt_client_publish(client, status_topic, status, 0, 1, 0);
    //         ESP_LOGI(TAG, "Published status to %s: %s, msg_id=%d", status_topic, status, msg_id);
    //     }
}

void turn_on_single_led()
{
    ESP_LOGI(TAG, "Switching to SINGLE LED mode...");
    turn_off_rgb();
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 1);
    ESP_LOGI(TAG, "Single LED turned ON");
    publish_device_status("single_led_on"); // Gửi trạng thái
}

void turn_off_single_led()
{
    gpio_set_level(LED_GPIO, 0);
    ESP_LOGI(TAG, "Single LED turned OFF");
    publish_device_status("single_led_off"); // Gửi trạng thái
}

void turn_on_rgb()
{
    turn_off_single_led(); // Tắt LED đơn trước khi bật RGB

    if (led_strip == NULL)
    {
        ESP_LOGI(TAG, "Reinitializing LED Strip...");
        configure_led(); // Cấu hình lại LED RGB
    }

    if (cycle_colors_task_handle == NULL)
    {
        xTaskCreate(&cycle_colors, "cycle_colors_task", 4096, NULL, 5, &cycle_colors_task_handle);
    }

    ESP_LOGI(TAG, "RGB LED turned ON");
    publish_device_status("rgb_on"); // Gửi trạng thái
}

void turn_off_rgb()
{
    ESP_LOGI(TAG, "Turning off RGB LED");

    if (cycle_colors_task_handle != NULL)
    {
        vTaskDelete(cycle_colors_task_handle);
        cycle_colors_task_handle = NULL;
    }

    if (led_strip != NULL)
    {
        led_strip_clear(led_strip);                // Tắt LED
        ESP_ERROR_CHECK(led_strip_del(led_strip)); // Giải phóng LED Strip
        led_strip = NULL;
    }
    turn_off_single_led();            // Tắt LED đơn trước khi bật RGB
    publish_device_status("rgb_off"); // Gửi trạng thái
}

void reset_gpio_for_single_led()
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

void init_single_led()
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    bool provisioned = false; // Check NVS later if needed
    if (load_wifi_credentials(stored_ssid, MAX_SSID_LEN, stored_password, MAX_PASS_LEN) == ESP_OK)
    {
        provisioned = true;
        ESP_LOGI(TAG, "Device already provisioned with SSID: %s", stored_ssid);
    }
    if (!provisioned)
    {
        ESP_LOGI(TAG, "Starting provisioning");
        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

        wifi_prov_security_t security = WIFI_PROV_SECURITY_2;
        const char *username = EXAMPLE_PROV_SEC2_USERNAME;
        const char *pop = EXAMPLE_PROV_SEC2_PWD;

        wifi_prov_security2_params_t sec2_params = {};
        ESP_ERROR_CHECK(example_get_sec2_salt(&sec2_params.salt, &sec2_params.salt_len));
        ESP_ERROR_CHECK(example_get_sec2_verifier(&sec2_params.verifier, &sec2_params.verifier_len));

        const char *service_key = NULL;
        uint8_t custom_service_uuid[] = {
            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02};
        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *)&sec2_params, service_name, service_key));
        wifi_prov_print_qr(service_name, username, pop, PROV_TRANSPORT_BLE);
    }
    else
    {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
        wifi_prov_mgr_deinit();
    }
    wifi_init_sta();
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi Connected, SSID: %s, Password: %s", stored_ssid, stored_password);

    // Start MQTT after Wi-Fi is connected
    mqtt_app_start();

    // Keep the app running
}