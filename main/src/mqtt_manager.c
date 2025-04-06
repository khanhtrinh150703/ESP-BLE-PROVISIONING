#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <mqtt_client.h>

#include "mqtt_manager.h"
#include "command_handler.h"

static const char *TAG = "mqtt_manager";
static esp_mqtt_client_handle_t client = NULL;
static uint32_t MQTT_CONNECTED = 0;
static bool isPublish = false;

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to broker");
        MQTT_CONNECTED = 1;
        char command_topic[64];
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(command_topic, sizeof(command_topic), "/devices/esp_device_%02X%02X%02X/command", mac[3], mac[4], mac[5]);
        int msg_id = esp_mqtt_client_subscribe(client, command_topic, 0);
        ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", command_topic, msg_id);

        char status_topic[64];
        snprintf(status_topic, sizeof(status_topic), "/speech/command");
        int msg_id_status = esp_mqtt_client_subscribe(client, status_topic, 0);
        ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", status_topic, msg_id_status);
        
        if (!isPublish)
        {
            int msg_id2 = esp_mqtt_client_publish(client, "/devices/notification", command_topic, 0, 1, 0);
            ESP_LOGI(TAG, "Published command topic to /devices/notification: %s, msg_id=%d", command_topic, msg_id2);
            isPublish = true;
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected from broker");
        MQTT_CONNECTED = 0;
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

void mqtt_manager_init(void)
{
    ESP_LOGI(TAG, "Starting MQTT client...");
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
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
}