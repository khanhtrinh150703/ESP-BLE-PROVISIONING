#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <esp_err.h>
#include <esp_mac.h>
#include <freertos/event_groups.h>
#include <esp_netif.h>

#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64

extern EventGroupHandle_t wifi_event_group;
extern const int WIFI_CONNECTED_EVENT;

void wifi_manager_init(void);
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
const char *wifi_get_ssid(void);
const char *wifi_get_password(void);

#endif // WIFI_MANAGER_H
