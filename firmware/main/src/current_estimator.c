#include "current_estimator.h"

#include <math.h>

float current_estimator_estimate_a(const thermal_features_t *features)
{
    if (features == NULL || features->temp_rise_c <= 0.0f) {
        return 0.0f;
    }

    // 占位模型：导线温升近似与电流平方相关。
    // k 需要后续用你的实测数据按导线规格重新拟合。
    const float k_temp_rise_per_a2 = 0.20f;
    float current = sqrtf(features->temp_rise_c / k_temp_rise_per_a2);

    if (features->heating_rate_c_per_min > 4.0f) {
        current += 0.5f;
    }

    return current;
}

