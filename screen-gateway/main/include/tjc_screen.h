#pragma once

#include "esp_err.h"
#include "gateway_telemetry.h"

esp_err_t tjc_screen_init(void);
esp_err_t tjc_screen_show_status(const char *state_text);
esp_err_t tjc_screen_update(const gateway_telemetry_t *telemetry);
