#pragma once

#include "app_types.h"

overload_result_t overload_detector_update(const thermal_features_t *features, float estimated_current_a);
overload_result_t overload_detector_update_from_prediction(const thermal_features_t *features,
                                                           const model_prediction_t *prediction);
void overload_detector_reset(void);
void overload_detector_apply_state_machine(overload_result_t *result,
                                           const thermal_features_t *features,
                                           int64_t now_ms);
const char *app_state_to_string(app_state_t state);
