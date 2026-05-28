#pragma once

#include <stdbool.h>

#include "app_types.h"
#include "esp_err.h"

esp_err_t mqtt_bridge_start(void);
bool mqtt_bridge_is_connected(void);
esp_err_t mqtt_bridge_publish_json(const char *payload);
esp_err_t mqtt_bridge_publish_status(const sensor_sample_t *sample,
                                     const thermal_features_t *features,
                                     const overload_result_t *result,
                                     const runtime_stats_t *stats);

