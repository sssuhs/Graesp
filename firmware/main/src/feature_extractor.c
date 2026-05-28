#include "feature_extractor.h"

#include <math.h>
#include <stdbool.h>

static bool s_has_last;
static float s_last_avg_temp_c;
static int64_t s_last_time_ms;

void feature_extractor_reset(void)
{
    s_has_last = false;
    s_last_avg_temp_c = 0.0f;
    s_last_time_ms = 0;
}

thermal_features_t feature_extractor_update(const sensor_sample_t *sample, int64_t now_ms)
{
    thermal_features_t out = {0};
    if (sample == NULL) {
        return out;
    }

    const float cable_1 = sample->cable_temp_c[0];
    const float cable_2 = sample->cable_temp_c[1];

    out.cable_avg_temp_c = (cable_1 + cable_2) * 0.5f;
    out.cable_max_temp_c = fmaxf(cable_1, cable_2);
    out.ambient_temp_c = sample->ambient_temp_c;
    out.temp_rise_c = out.cable_max_temp_c - out.ambient_temp_c;
    out.point_diff_c = fabsf(cable_1 - cable_2);

    if (s_has_last && now_ms > s_last_time_ms) {
        float dt_min = (float)(now_ms - s_last_time_ms) / 60000.0f;
        out.heating_rate_c_per_min = (out.cable_avg_temp_c - s_last_avg_temp_c) / dt_min;
    } else {
        out.heating_rate_c_per_min = 0.0f;
    }

    s_has_last = true;
    s_last_avg_temp_c = out.cable_avg_temp_c;
    s_last_time_ms = now_ms;
    return out;
}
