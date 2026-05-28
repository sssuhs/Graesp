#include "runtime_stats.h"

#include <string.h>

void runtime_stats_init(runtime_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

void runtime_stats_set_self_test(runtime_stats_t *stats, bool ok, uint32_t fault_mask)
{
    if (stats == NULL) {
        return;
    }
    stats->self_test_done = true;
    stats->self_test_ok = ok;
    stats->self_test_fault_mask = fault_mask;
}

void runtime_stats_update(runtime_stats_t *stats,
                          const thermal_features_t *features,
                          const overload_result_t *result)
{
    if (stats == NULL || result == NULL) {
        return;
    }

    if (!stats->has_last_state || stats->last_state != result->state) {
        switch (result->state) {
        case APP_STATE_TEMP_HIGH:
            stats->temp_high_count++;
            break;
        case APP_STATE_WARNING:
            stats->warning_count++;
            break;
        case APP_STATE_OVERLOAD:
            stats->alarm_count++;
            break;
        case APP_STATE_LOW_BATTERY:
            stats->low_battery_count++;
            break;
        case APP_STATE_FAULT:
            stats->fault_count++;
            break;
        case APP_STATE_NORMAL:
        default:
            break;
        }
        stats->has_last_state = true;
        stats->last_state = result->state;
    }

    if (features != NULL && features->temp_rise_c > stats->max_temp_rise_c) {
        stats->max_temp_rise_c = features->temp_rise_c;
    }
    if (result->estimated_current_a > stats->max_estimated_current_a) {
        stats->max_estimated_current_a = result->estimated_current_a;
    }
}
