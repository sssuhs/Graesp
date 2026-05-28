#include "telemetry_json.h"

#include <stdio.h>

#include "app_config.h"
#include "device_identity.h"
#include "overload_detector.h"

static const char *battery_state_to_string(battery_state_t state)
{
    switch (state) {
    case APP_BATTERY_NORMAL:
        return "normal";
    case APP_BATTERY_LOW:
        return "low";
    case APP_BATTERY_CRITICAL:
    default:
        return "critical";
    }
}

int telemetry_json_build(char *buffer,
                         size_t buffer_size,
                         int64_t uptime_ms,
                         const sensor_sample_t *sample,
                         const thermal_features_t *features,
                         const overload_result_t *result,
                         const runtime_stats_t *stats)
{
    if (buffer == NULL || buffer_size == 0 || sample == NULL || features == NULL ||
        result == NULL || stats == NULL) {
        return -1;
    }

    return snprintf(buffer,
                    buffer_size,
                    "{\"type\":\"telemetry\",\"device_id\":\"%s\",\"uptime_ms\":%lld,"
                    "\"ntc_c\":[%.2f,%.2f,%.2f],\"ntc_ok\":[%s,%s,%s],\"adc_mv\":[%d,%d,%d],"
                    "\"cable_avg_c\":%.2f,\"cable_max_c\":%.2f,\"ambient_c\":%.2f,"
                    "\"temp_rise_c\":%.2f,\"point_diff_c\":%.2f,\"heating_rate_c_per_min\":%.2f,"
                    "\"estimated_current_a\":%.2f,\"overload_probability\":%.2f,"
                    "\"state\":\"%s\",\"battery_v\":%.2f,\"battery_percent\":%d,"
                    "\"battery_ok\":%s,\"battery_state\":\"%s\","
                    "\"self_test_ok\":%s,\"self_test_fault_mask\":%lu,"
                    "\"temp_high_count\":%lu,\"alarm_count\":%lu,\"warning_count\":%lu,\"fault_count\":%lu,"
                    "\"low_battery_count\":%lu,\"max_temp_rise_c\":%.2f,"
                    "\"max_estimated_current_a\":%.2f}",
                    device_identity_get(),
                    (long long)uptime_ms,
                    sample->ntc_temp_c[0],
                    sample->ntc_temp_c[1],
                    sample->ntc_temp_c[2],
                    sample->ntc_valid[0] ? "true" : "false",
                    sample->ntc_valid[1] ? "true" : "false",
                    sample->ntc_valid[2] ? "true" : "false",
                    sample->raw_mv[0],
                    sample->raw_mv[1],
                    sample->raw_mv[2],
                    features->cable_avg_temp_c,
                    features->cable_max_temp_c,
                    features->ambient_temp_c,
                    features->temp_rise_c,
                    features->point_diff_c,
                    features->heating_rate_c_per_min,
                    result->estimated_current_a,
                    result->overload_probability,
                    app_state_to_string(result->state),
                    sample->battery_voltage_v,
                    sample->battery_percent,
                    sample->battery_valid ? "true" : "false",
                    battery_state_to_string(sample->battery_state),
                    stats->self_test_ok ? "true" : "false",
                    (unsigned long)stats->self_test_fault_mask,
                    (unsigned long)stats->temp_high_count,
                    (unsigned long)stats->alarm_count,
                    (unsigned long)stats->warning_count,
                    (unsigned long)stats->fault_count,
                    (unsigned long)stats->low_battery_count,
                    stats->max_temp_rise_c,
                    stats->max_estimated_current_a);
}
