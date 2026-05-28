#include "overload_detector.h"

#include "app_config.h"

static app_state_t s_confirmed_state = APP_STATE_NORMAL;
static app_state_t s_candidate_state = APP_STATE_NORMAL;
static int64_t s_candidate_since_ms = 0;

static int state_severity(app_state_t state)
{
    switch (state) {
    case APP_STATE_OVERLOAD:
        return 4;
    case APP_STATE_WARNING:
        return 3;
    case APP_STATE_TEMP_HIGH:
        return 2;
    case APP_STATE_NORMAL:
        return 1;
    case APP_STATE_LOW_BATTERY:
        return 0;
    case APP_STATE_FAULT:
    default:
        return 4;
    }
}

static int64_t confirm_ms_for_state(app_state_t state)
{
    switch (state) {
    case APP_STATE_OVERLOAD:
        return APP_OVERLOAD_CONFIRM_MS;
    case APP_STATE_WARNING:
        return APP_WARNING_CONFIRM_MS;
    case APP_STATE_TEMP_HIGH:
        return APP_TEMP_HIGH_CONFIRM_MS;
    case APP_STATE_NORMAL:
    default:
        return APP_STATE_RECOVERY_MS;
    }
}

static bool release_ok(app_state_t from_state, const thermal_features_t *features)
{
    if (features == NULL) {
        return false;
    }

    switch (from_state) {
    case APP_STATE_OVERLOAD:
        return features->temp_rise_c <= APP_OVERLOAD_RELEASE_TEMP_RISE_C &&
               features->heating_rate_c_per_min <= APP_OVERLOAD_RELEASE_RATE_C_PER_MIN;
    case APP_STATE_WARNING:
        return features->temp_rise_c <= APP_WARNING_RELEASE_TEMP_RISE_C &&
               features->heating_rate_c_per_min <= APP_WARNING_RELEASE_RATE_C_PER_MIN;
    case APP_STATE_TEMP_HIGH:
        return features->temp_rise_c <= APP_TEMP_HIGH_RELEASE_TEMP_RISE_C &&
               features->heating_rate_c_per_min <= APP_TEMP_HIGH_RELEASE_RATE_C_PER_MIN;
    default:
        return true;
    }
}

static bool candidate_stable(app_state_t target, int64_t now_ms, int64_t required_ms)
{
    if (s_candidate_state != target) {
        s_candidate_state = target;
        s_candidate_since_ms = now_ms;
        return required_ms <= 0;
    }

    return (now_ms - s_candidate_since_ms) >= required_ms;
}

void overload_detector_reset(void)
{
    s_confirmed_state = APP_STATE_NORMAL;
    s_candidate_state = APP_STATE_NORMAL;
    s_candidate_since_ms = 0;
}

const char *app_state_to_string(app_state_t state)
{
    switch (state) {
    case APP_STATE_NORMAL:
        return "normal";
    case APP_STATE_TEMP_HIGH:
        return "temp_high";
    case APP_STATE_WARNING:
        return "warning";
    case APP_STATE_OVERLOAD:
        return "overload";
    case APP_STATE_LOW_BATTERY:
        return "low_battery";
    case APP_STATE_FAULT:
    default:
        return "fault";
    }
}

overload_result_t overload_detector_update(const thermal_features_t *features, float estimated_current_a)
{
    overload_result_t out = {
        .estimated_current_a = estimated_current_a,
        .overload_probability = 0.0f,
        .state = APP_STATE_NORMAL,
    };

    if (features == NULL) {
        out.state = APP_STATE_FAULT;
        return out;
    }

    bool overload = (estimated_current_a >= APP_OVERLOAD_CURRENT_A &&
                     features->temp_rise_c >= APP_MODEL_OVERLOAD_MIN_TEMP_RISE_C) ||
                    features->temp_rise_c >= APP_OVERLOAD_TEMP_RISE_C;
    bool warning = (estimated_current_a >= APP_WARNING_CURRENT_A &&
                    features->temp_rise_c >= APP_MODEL_WARNING_MIN_TEMP_RISE_C) ||
                   features->temp_rise_c >= APP_WARNING_TEMP_RISE_C ||
                   features->heating_rate_c_per_min >= 6.0f;
    bool temp_high = features->temp_rise_c >= APP_TEMP_HIGH_TEMP_RISE_C ||
                     features->heating_rate_c_per_min >= 3.0f;

    if (overload) {
        out.state = APP_STATE_OVERLOAD;
        out.overload_probability = 0.95f;
    } else if (warning) {
        out.state = APP_STATE_WARNING;
        out.overload_probability = 0.65f;
    } else if (temp_high) {
        out.state = APP_STATE_TEMP_HIGH;
        out.overload_probability = 0.30f;
    } else {
        out.state = APP_STATE_NORMAL;
        out.overload_probability = 0.10f;
    }

    return out;
}

void overload_detector_apply_state_machine(overload_result_t *result,
                                           const thermal_features_t *features,
                                           int64_t now_ms)
{
    if (result == NULL) {
        return;
    }

    const app_state_t target = result->state;

    if (target == APP_STATE_FAULT) {
        s_confirmed_state = APP_STATE_FAULT;
        s_candidate_state = APP_STATE_FAULT;
        s_candidate_since_ms = now_ms;
        result->state = APP_STATE_FAULT;
        return;
    }

    if (s_confirmed_state == APP_STATE_FAULT) {
        if (candidate_stable(target, now_ms, APP_STATE_RECOVERY_MS)) {
            s_confirmed_state = target;
        }
        result->state = s_confirmed_state;
        return;
    }

    const int target_severity = state_severity(target);
    const int current_severity = state_severity(s_confirmed_state);

    if (target_severity > current_severity) {
        if (candidate_stable(target, now_ms, confirm_ms_for_state(target))) {
            s_confirmed_state = target;
        }
    } else if (target_severity < current_severity) {
        if (release_ok(s_confirmed_state, features) &&
            candidate_stable(target, now_ms, APP_STATE_RECOVERY_MS)) {
            s_confirmed_state = target;
        }
    } else {
        s_candidate_state = target;
        s_candidate_since_ms = now_ms;
        s_confirmed_state = target;
    }

    result->state = s_confirmed_state;
}

overload_result_t overload_detector_update_from_prediction(const thermal_features_t *features,
                                                           const model_prediction_t *prediction)
{
    if (prediction == NULL) {
        overload_result_t out = {
            .estimated_current_a = 0.0f,
            .overload_probability = 0.0f,
            .state = APP_STATE_FAULT,
        };
        return out;
    }

    overload_result_t out = overload_detector_update(features, prediction->estimated_current_a);

    if (features == NULL) {
        out.state = APP_STATE_FAULT;
        return out;
    }

    float probability = prediction->overload_probability;
    if (features->temp_rise_c < APP_MODEL_WARNING_MIN_TEMP_RISE_C &&
        probability > APP_MODEL_LOW_RISE_PROBABILITY_CAP) {
        probability = APP_MODEL_LOW_RISE_PROBABILITY_CAP;
    } else if (features->temp_rise_c < APP_MODEL_OVERLOAD_MIN_TEMP_RISE_C &&
               probability > APP_MODEL_PRE_OVERLOAD_PROBABILITY_CAP) {
        probability = APP_MODEL_PRE_OVERLOAD_PROBABILITY_CAP;
    }
    out.overload_probability = probability;

    const bool enough_rise_for_warning = features->temp_rise_c >= APP_MODEL_WARNING_MIN_TEMP_RISE_C;
    const bool enough_rise_for_overload = features->temp_rise_c >= APP_MODEL_OVERLOAD_MIN_TEMP_RISE_C;
    const bool enough_rise_for_temp_high = features->temp_rise_c >= APP_TEMP_HIGH_TEMP_RISE_C;
    const bool model_overload = probability >= APP_MODEL_OVERLOAD_PROBABILITY && enough_rise_for_overload;
    const bool model_warning = probability >= APP_MODEL_WARNING_PROBABILITY && enough_rise_for_warning;
    const bool hard_overload = (prediction->estimated_current_a >= APP_OVERLOAD_CURRENT_A &&
                                enough_rise_for_overload) ||
                               features->temp_rise_c >= APP_OVERLOAD_TEMP_RISE_C;
    const bool hard_warning = (prediction->estimated_current_a >= APP_WARNING_CURRENT_A &&
                               enough_rise_for_warning) ||
                              features->temp_rise_c >= APP_WARNING_TEMP_RISE_C ||
                              features->heating_rate_c_per_min >= 6.0f;
    const bool hard_temp_high = enough_rise_for_temp_high ||
                                features->heating_rate_c_per_min >= 3.0f;

    if (model_overload || hard_overload) {
        out.state = APP_STATE_OVERLOAD;
    } else if (model_warning || hard_warning) {
        out.state = APP_STATE_WARNING;
    } else if (hard_temp_high) {
        out.state = APP_STATE_TEMP_HIGH;
    } else {
        out.state = APP_STATE_NORMAL;
    }

    return out;
}

