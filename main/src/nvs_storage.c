#include <stdio.h>
#include <esp_log.h>
#include <nvs.h>

#include "nvs_storage.h"

static const char *TAG = "nvs_storage";

esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK) goto cleanup;
    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK) goto cleanup;
    err = nvs_commit(nvs_handle);

cleanup:
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Wi-Fi credentials saved to NVS: SSID=%s, Password=%s", ssid, password);
    return err;
}

esp_err_t load_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    if (err != ESP_OK) goto cleanup;
    err = nvs_get_str(nvs_handle, "password", password, &pass_len);

cleanup:
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Wi-Fi credentials loaded from NVS: SSID=%s, Password=%s", ssid, password);
    return err;
}

esp_err_t erase_wifi_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    err = nvs_erase_key(nvs_handle, "ssid");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto cleanup;
    err = nvs_erase_key(nvs_handle, "password");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto cleanup;
    err = nvs_commit(nvs_handle);

cleanup:
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Wi-Fi credentials erased from NVS");
    return err;
}

