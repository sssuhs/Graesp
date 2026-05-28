#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_STATE_NORMAL = 0,
    APP_STATE_TEMP_HIGH,
    APP_STATE_WARNING,
    APP_STATE_OVERLOAD,
    APP_STATE_LOW_BATTERY,
    APP_STATE_FAULT,
} app_state_t;

typedef enum {
    APP_BATTERY_NORMAL = 0,
    APP_BATTERY_LOW,
    APP_BATTERY_CRITICAL,
} battery_state_t;

typedef struct {
    int raw_mv[3];
    bool ntc_valid[3];
    float ntc_temp_c[3];
    float cable_temp_c[2];
    float ambient_temp_c;
    bool battery_valid;
    battery_state_t battery_state;
    float battery_voltage_v;
    int battery_percent;
} sensor_sample_t;

typedef struct {
    float cable_avg_temp_c;
    float cable_max_temp_c;
    float ambient_temp_c;
    float temp_rise_c;
    float point_diff_c;
    float heating_rate_c_per_min;
} thermal_features_t;

typedef struct {
    float estimated_current_a;
    float overload_probability;
} model_prediction_t;

typedef struct {
    float estimated_current_a;
    float overload_probability;
    app_state_t state;
} overload_result_t;

typedef struct {
    bool self_test_done;
    bool self_test_ok;
    uint32_t self_test_fault_mask;
    uint32_t temp_high_count;
    uint32_t alarm_count;
    uint32_t warning_count;
    uint32_t fault_count;
    uint32_t low_battery_count;
    float max_temp_rise_c;
    float max_estimated_current_a;
    bool has_last_state;
    app_state_t last_state;
} runtime_stats_t;
