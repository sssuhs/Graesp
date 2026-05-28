#pragma once

#include "esp_err.h"

esp_err_t telemetry_udp_init(void);
esp_err_t telemetry_udp_send(const char *payload);

