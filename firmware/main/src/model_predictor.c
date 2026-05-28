#include "model_predictor.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "current_estimator.h"
#include "model_weights.h"

static float s_sequence[MODEL_SEQUENCE_LENGTH][MODEL_FEATURE_COUNT];
static size_t s_write_index;
static size_t s_valid_count;

static float clampf_local(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float sigmoidf_local(float x)
{
    x = clampf_local(x, -20.0f, 20.0f);
    return 1.0f / (1.0f + expf(-x));
}

static float reluf_local(float x)
{
    return x > 0.0f ? x : 0.0f;
}

static float normalize_feature(size_t index, float value)
{
    const float scale = MODEL_FEATURE_SCALE[index] == 0.0f ? 1.0f : MODEL_FEATURE_SCALE[index];
    return clampf_local((value - MODEL_FEATURE_MEAN[index]) / scale, -4.0f, 4.0f);
}

static void push_features(const sensor_sample_t *sample, const thermal_features_t *features)
{
    float row[MODEL_FEATURE_COUNT] = {
        features->temp_rise_c,
        features->heating_rate_c_per_min,
        features->point_diff_c,
        features->cable_avg_temp_c,
        features->cable_max_temp_c,
        features->ambient_temp_c,
        sample->battery_voltage_v,
    };

    for (size_t i = 0; i < MODEL_FEATURE_COUNT; ++i) {
        s_sequence[s_write_index][i] = normalize_feature(i, row[i]);
    }

    s_write_index = (s_write_index + 1U) % MODEL_SEQUENCE_LENGTH;
    if (s_valid_count < MODEL_SEQUENCE_LENGTH) {
        s_valid_count++;
    }
}

static const float *sequence_at(size_t logical_index)
{
    size_t start = 0;
    if (s_valid_count == MODEL_SEQUENCE_LENGTH) {
        start = s_write_index;
    }
    const size_t physical_index = (start + logical_index) % MODEL_SEQUENCE_LENGTH;
    return s_sequence[physical_index];
}

static void run_temporal_model(float hidden[MODEL_GRU_UNITS])
{
    for (size_t unit = 0; unit < MODEL_GRU_UNITS; ++unit) {
        hidden[unit] = 0.0f;
    }

    if (s_valid_count == 0) {
        return;
    }

    for (size_t t = 0; t < s_valid_count; ++t) {
        float conv_out[MODEL_CONV_CHANNELS] = {0};

        for (size_t ch = 0; ch < MODEL_CONV_CHANNELS; ++ch) {
            float sum = MODEL_CONV_BIAS[ch];
            for (size_t k = 0; k < 3; ++k) {
                size_t sample_index = t;
                if (k == 0 && t > 0) {
                    sample_index = t - 1U;
                } else if (k == 2 && (t + 1U) < s_valid_count) {
                    sample_index = t + 1U;
                }

                const float *row = sequence_at(sample_index);
                for (size_t f = 0; f < MODEL_FEATURE_COUNT; ++f) {
                    sum += row[f] * MODEL_CONV_KERNEL[ch][k][f];
                }
            }
            conv_out[ch] = reluf_local(sum);
        }

        for (size_t unit = 0; unit < MODEL_GRU_UNITS; ++unit) {
            float candidate = MODEL_GRU_BIAS[unit] + hidden[unit] * MODEL_GRU_RECURRENT_WEIGHT[unit];
            for (size_t ch = 0; ch < MODEL_CONV_CHANNELS; ++ch) {
                candidate += conv_out[ch] * MODEL_GRU_INPUT_WEIGHT[unit][ch];
            }

            const float gate = sigmoidf_local(0.8f * candidate);
            const float new_value = tanhf(candidate);
            hidden[unit] = hidden[unit] * (1.0f - gate) + new_value * gate;
        }
    }
}

void model_predictor_reset(void)
{
    for (size_t t = 0; t < MODEL_SEQUENCE_LENGTH; ++t) {
        for (size_t f = 0; f < MODEL_FEATURE_COUNT; ++f) {
            s_sequence[t][f] = 0.0f;
        }
    }
    s_write_index = 0;
    s_valid_count = 0;
}

model_prediction_t model_predictor_predict(const sensor_sample_t *sample,
                                           const thermal_features_t *features)
{
    model_prediction_t out = {
        .estimated_current_a = 0.0f,
        .overload_probability = 0.0f,
    };

    if (sample == NULL || features == NULL) {
        return out;
    }

    push_features(sample, features);

    float hidden[MODEL_GRU_UNITS] = {0};
    run_temporal_model(hidden);

    float current = MODEL_CURRENT_HEAD_BIAS;
    float probability_score = MODEL_PROB_HEAD_BIAS;
    for (size_t unit = 0; unit < MODEL_GRU_UNITS; ++unit) {
        current += hidden[unit] * MODEL_CURRENT_HEAD_WEIGHT[unit];
        probability_score += hidden[unit] * MODEL_PROB_HEAD_WEIGHT[unit];
    }

    const float baseline_current = current_estimator_estimate_a(features);
    current = 0.65f * current + 0.35f * baseline_current;

    out.estimated_current_a = clampf_local(current, 0.0f, 30.0f);
#ifdef MODEL_PROB_CURRENT_WEIGHT
    probability_score += out.estimated_current_a * MODEL_PROB_CURRENT_WEIGHT;
#endif
    out.overload_probability = sigmoidf_local(probability_score);
    return out;
}

const char *model_predictor_version(void)
{
    return MODEL_PREDICTOR_VERSION;
}
