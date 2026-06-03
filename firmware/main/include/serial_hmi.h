#pragma once

#include "app_types.h"
#include "esp_err.h"

esp_err_t serial_hmi_init(void);
esp_err_t serial_hmi_send_status(int64_t uptime_ms,
                                 const sensor_sample_t *sample,
                                 const thermal_features_t *features,
                                 const overload_result_t *result,
                                 const runtime_stats_t *stats);
