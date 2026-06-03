#pragma once

#include "esp_err.h"

esp_err_t gateway_wifi_start(void);
bool gateway_wifi_wait_connected(int timeout_ms);
