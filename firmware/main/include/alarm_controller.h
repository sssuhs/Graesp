#pragma once

#include "app_types.h"
#include "esp_err.h"

esp_err_t alarm_controller_init(void);
void alarm_controller_update(app_state_t state, int64_t now_ms);
