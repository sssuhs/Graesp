#pragma once

#include "app_types.h"

void feature_extractor_reset(void);
thermal_features_t feature_extractor_update(const sensor_sample_t *sample, int64_t now_ms);

