#pragma once

#include "app_types.h"

void model_predictor_reset(void);
model_prediction_t model_predictor_predict(const sensor_sample_t *sample,
                                           const thermal_features_t *features);
const char *model_predictor_version(void);

