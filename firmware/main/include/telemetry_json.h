#pragma once

#include <stddef.h>
#include "app_types.h"

int telemetry_json_build(char *buffer,
                         size_t buffer_size,
                         int64_t uptime_ms,
                         const sensor_sample_t *sample,
                         const thermal_features_t *features,
                         const overload_result_t *result,
                         const runtime_stats_t *stats);
