#include "gateway_telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_value(const char *json, const char *key)
{
    if (json == NULL || key == NULL) {
        return NULL;
    }

    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return NULL;
    }
    pos = strchr(pos, ':');
    if (pos == NULL) {
        return NULL;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    return pos;
}

static bool parse_string(const char *json, const char *key, char *out, size_t out_size)
{
    const char *pos = find_value(json, key);
    if (pos == NULL || *pos != '"' || out == NULL || out_size == 0) {
        return false;
    }

    pos++;
    size_t used = 0;
    while (*pos != '\0' && *pos != '"' && used + 1 < out_size) {
        out[used++] = *pos++;
    }
    out[used] = '\0';
    return used > 0;
}

static bool parse_float(const char *json, const char *key, float *out)
{
    const char *pos = find_value(json, key);
    if (pos == NULL || out == NULL) {
        return false;
    }

    char *end = NULL;
    const float value = strtof(pos, &end);
    if (end == pos) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_int(const char *json, const char *key, int *out)
{
    const char *pos = find_value(json, key);
    if (pos == NULL || out == NULL) {
        return false;
    }

    char *end = NULL;
    const long value = strtol(pos, &end, 10);
    if (end == pos) {
        return false;
    }
    *out = (int)value;
    return true;
}

static bool parse_bool(const char *json, const char *key, bool *out)
{
    const char *pos = find_value(json, key);
    if (pos == NULL || out == NULL) {
        return false;
    }
    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_ulong(const char *json, const char *key, unsigned long *out)
{
    const char *pos = find_value(json, key);
    if (pos == NULL || out == NULL) {
        return false;
    }

    char *end = NULL;
    const unsigned long value = strtoul(pos, &end, 10);
    if (end == pos) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_ntc_array(const char *json, float *ntc1, float *ntc2)
{
    const char *pos = find_value(json, "ntc_c");
    if (pos == NULL || *pos != '[' || ntc1 == NULL || ntc2 == NULL) {
        return false;
    }
    pos++;

    char *end = NULL;
    *ntc1 = strtof(pos, &end);
    if (end == pos) {
        return false;
    }

    pos = strchr(end, ',');
    if (pos == NULL) {
        return false;
    }
    pos++;
    *ntc2 = strtof(pos, &end);
    return end != pos;
}

bool gateway_telemetry_parse(const char *json, gateway_telemetry_t *out)
{
    if (json == NULL || out == NULL) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    bool ok = true;
    ok = parse_string(json, "device_id", out->device_id, sizeof(out->device_id)) && ok;
    ok = parse_string(json, "state", out->state, sizeof(out->state)) && ok;
    ok = parse_float(json, "temp_rise_c", &out->temp_rise_c) && ok;
    ok = parse_float(json, "estimated_current_a", &out->estimated_current_a) && ok;
    ok = parse_float(json, "overload_probability", &out->overload_probability) && ok;
    ok = parse_int(json, "battery_percent", &out->battery_percent) && ok;
    ok = parse_float(json, "ambient_c", &out->ambient_c) && ok;
    ok = parse_float(json, "heating_rate_c_per_min", &out->heating_rate_c_per_min) && ok;
    ok = parse_bool(json, "self_test_ok", &out->self_test_ok) && ok;
    ok = parse_ulong(json, "self_test_fault_mask", &out->self_test_fault_mask) && ok;
    ok = parse_ntc_array(json, &out->ntc1_c, &out->ntc2_c) && ok;

    return ok;
}
