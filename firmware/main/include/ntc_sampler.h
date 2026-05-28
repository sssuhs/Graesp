#pragma once

#include "app_types.h"
#include "esp_err.h"

esp_err_t ntc_sampler_init(void);
esp_err_t ntc_sampler_read(sensor_sample_t *out_sample);

