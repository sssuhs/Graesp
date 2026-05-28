#include "ntc_sampler.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "ntc_sampler";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;
static bool s_cali_enabled;
static bool s_filter_ready;
static float s_filtered_temp_c[3];

static const adc_channel_t s_ntc_channels[3] = {
    APP_NTC1_ADC_CHANNEL,
    APP_NTC2_ADC_CHANNEL,
    APP_NTC_ENV_ADC_CHANNEL,
};

static void ntc_power_set(bool enabled)
{
    gpio_set_level(APP_NTC_POWER_GPIO, enabled ? 1 : 0);
}

static esp_err_t adc_read_mv(adc_channel_t channel, int *out_mv)
{
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, channel, &raw);
    if (err != ESP_OK) {
        return err;
    }

    if (s_cali_enabled) {
        return adc_cali_raw_to_voltage(s_cali_handle, raw, out_mv);
    }

    *out_mv = (int)((float)raw * APP_ADC_VREF_MV / 4095.0f);
    return ESP_OK;
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        calibrated = (ret == ESP_OK);
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        calibrated = (ret == ESP_OK);
    }
#endif

    *out_handle = handle;
    if (!calibrated && ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "adc calibration unavailable: %s", esp_err_to_name(ret));
    }
    return calibrated;
}

static float ntc_mv_to_temp_c(int mv)
{
    float v = (float)mv;
    if (v < 1.0f) {
        v = 1.0f;
    }
    if (v > APP_ADC_VREF_MV - 1.0f) {
        v = APP_ADC_VREF_MV - 1.0f;
    }

    float r_ntc = APP_NTC_R_FIXED_OHM * v / (APP_ADC_VREF_MV - v);
    float inv_t = (1.0f / APP_NTC_T0_K) + (logf(r_ntc / APP_NTC_R0_OHM) / APP_NTC_BETA);
    return (1.0f / inv_t) - 273.15f;
}

static bool ntc_reading_valid(int mv, float temp_c)
{
    return mv >= APP_NTC_ADC_MIN_MV &&
           mv <= APP_NTC_ADC_MAX_MV &&
           temp_c >= APP_NTC_MIN_TEMP_C &&
           temp_c <= APP_NTC_MAX_TEMP_C;
}

static float filter_temperature(int index, float value)
{
    if (!s_filter_ready) {
        s_filtered_temp_c[index] = value;
        return value;
    }

    s_filtered_temp_c[index] =
        (APP_FILTER_ALPHA * value) + ((1.0f - APP_FILTER_ALPHA) * s_filtered_temp_c[index]);
    return s_filtered_temp_c[index];
}

static float battery_mv_to_voltage_v(int mv)
{
    float divider_gain = (APP_BAT_R_TOP_OHM + APP_BAT_R_BOTTOM_OHM) / APP_BAT_R_BOTTOM_OHM;
    return ((float)mv / 1000.0f) * divider_gain;
}

static battery_state_t battery_state_from_voltage(float battery_v)
{
    if (battery_v <= APP_BATTERY_CRITICAL_V) {
        return APP_BATTERY_CRITICAL;
    }
    if (battery_v <= APP_BATTERY_LOW_V) {
        return APP_BATTERY_LOW;
    }
    return APP_BATTERY_NORMAL;
}

static int battery_percent_from_voltage(float battery_v)
{
    float percent =
        (battery_v - APP_BATTERY_EMPTY_V) * 100.0f / (APP_BATTERY_FULL_V - APP_BATTERY_EMPTY_V);
    if (percent < 0.0f) {
        return 0;
    }
    if (percent > 100.0f) {
        return 100;
    }
    return (int)(percent + 0.5f);
}

esp_err_t ntc_sampler_init(void)
{
    gpio_config_t ntc_power_config = {
        .pin_bit_mask = 1ULL << APP_NTC_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&ntc_power_config), TAG, "config ntc power gpio failed");
    ntc_power_set(true);
    esp_rom_delay_us(APP_NTC_POWER_SETTLE_US);

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_config, &s_adc_handle), TAG, "new adc unit failed");

    adc_oneshot_chan_cfg_t chan_config = {
        .atten = APP_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    for (size_t i = 0; i < 3; ++i) {
        ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, s_ntc_channels[i], &chan_config),
                            TAG,
                            "config ntc adc failed");
    }
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, APP_BAT_ADC_CHANNEL, &chan_config),
                        TAG,
                        "config battery adc failed");

    s_cali_enabled = adc_calibration_init(ADC_UNIT_1, s_ntc_channels[0], APP_ADC_ATTEN, &s_cali_handle);
    ESP_LOGI(TAG, "adc calibration: %s", s_cali_enabled ? "enabled" : "fallback raw scale");

    memset(s_filtered_temp_c, 0, sizeof(s_filtered_temp_c));
    s_filter_ready = false;
    return ESP_OK;
}

esp_err_t ntc_sampler_read(sensor_sample_t *out_sample)
{
    if (out_sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_sample, 0, sizeof(*out_sample));

    ntc_power_set(true);
    esp_rom_delay_us(APP_NTC_POWER_SETTLE_US);

    for (int i = 0; i < 3; ++i) {
        int mv = 0;
        ESP_RETURN_ON_ERROR(adc_read_mv(s_ntc_channels[i], &mv), TAG, "read ntc failed");
        float temp_c = ntc_mv_to_temp_c(mv);

        out_sample->raw_mv[i] = mv;
        out_sample->ntc_valid[i] = ntc_reading_valid(mv, temp_c);
        out_sample->ntc_temp_c[i] = filter_temperature(i, temp_c);
    }

    s_filter_ready = true;

    out_sample->cable_temp_c[0] = out_sample->ntc_temp_c[0];
    out_sample->cable_temp_c[1] = out_sample->ntc_temp_c[1];
    out_sample->ambient_temp_c = out_sample->ntc_temp_c[2];

    int bat_mv = 0;
    ESP_RETURN_ON_ERROR(adc_read_mv(APP_BAT_ADC_CHANNEL, &bat_mv), TAG, "read battery failed");
    out_sample->battery_voltage_v = battery_mv_to_voltage_v(bat_mv);
    out_sample->battery_valid = out_sample->battery_voltage_v >= APP_BATTERY_MIN_VALID_V &&
                                out_sample->battery_voltage_v <= APP_BATTERY_MAX_VALID_V;
    out_sample->battery_state = battery_state_from_voltage(out_sample->battery_voltage_v);
    out_sample->battery_percent =
        out_sample->battery_valid ? battery_percent_from_voltage(out_sample->battery_voltage_v) : 0;

    ntc_power_set(false);

    return ESP_OK;
}
