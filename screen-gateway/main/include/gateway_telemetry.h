#pragma once

#include <stdbool.h>

typedef struct {
    char device_id[32];
    char state[24];
    float temp_rise_c;
    float estimated_current_a;
    float overload_probability;
    int battery_percent;
    float ntc1_c;
    float ntc2_c;
    float ambient_c;
    float heating_rate_c_per_min;
    bool self_test_ok;
    unsigned long self_test_fault_mask;
} gateway_telemetry_t;

bool gateway_telemetry_parse(const char *json, gateway_telemetry_t *out);
