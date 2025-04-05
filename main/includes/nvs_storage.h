#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include <esp_err.h>

esp_err_t save_wifi_credentials(const char *ssid, const char *password);
esp_err_t load_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len);
esp_err_t erase_wifi_credentials(void);

#endif