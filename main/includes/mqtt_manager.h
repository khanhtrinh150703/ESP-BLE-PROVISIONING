#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <esp_mac.h>
#include <esp_err.h>
#define MAX_DATA_LENGTH 64


void mqtt_manager_init(void);
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

#endif