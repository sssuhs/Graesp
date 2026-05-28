#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types.h"

esp_err_t app_wifi_station_start(void);
esp_err_t app_wifi_station_try_builtin_debug(void);
bool app_wifi_station_wait_connected(uint32_t timeout_ms);
bool app_wifi_station_is_connected(void);
esp_err_t app_wifi_provisioning_start(void);
esp_err_t app_wifi_save_credentials_and_restart(const char *ssid, const char *password);
esp_err_t app_wifi_clear_credentials_and_restart(void);
esp_err_t app_wifi_scan(wifi_ap_record_t *records, uint16_t *record_count);
