#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "alarm_controller.h"
#include "command_udp.h"
#include "current_estimator.h"
#include "device_identity.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "feature_extractor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_predictor.h"
#include "mqtt_bridge.h"
#include "nvs_flash.h"
#include "ntc_sampler.h"
#include "overload_detector.h"
#include "runtime_stats.h"
#include "telemetry_json.h"
#include "telemetry_udp.h"
#include "wifi_station.h"

static const char *TAG = "graesp";

#define SELF_TEST_FAULT_NTC1 BIT0
#define SELF_TEST_FAULT_NTC2 BIT1
#define SELF_TEST_FAULT_NTC_ENV BIT2
#define SELF_TEST_FAULT_BATTERY BIT3
#define SELF_TEST_FAULT_SENSOR_READ BIT4

static wifi_ap_record_t s_wifi_scan_records[APP_WIFI_SCAN_MAX_AP];
static uint16_t s_wifi_scan_count;
static bool s_wifi_scan_ready;
static char s_wifi_scan_status[24] = "idle";
static char s_wifi_scan_error[32];


static size_t json_appendf(char *buffer, size_t buffer_size, size_t used, const char *format, ...)
{
    if (buffer == NULL || buffer_size == 0 || used >= buffer_size) {
        return used;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + used, buffer_size - used, format, args);
    va_end(args);

    if (written < 0) {
        return used;
    }
    if ((size_t)written >= buffer_size - used) {
        return buffer_size - 1;
    }
    return used + (size_t)written;
}

static size_t json_append_escaped(char *buffer, size_t buffer_size, size_t used, const char *text)
{
    if (text == NULL) {
        return used;
    }

    for (const unsigned char *pos = (const unsigned char *)text; *pos != '\0' && used + 2 < buffer_size; pos++) {
        if (*pos == '"' || *pos == '\\') {
            buffer[used++] = '\\';
            buffer[used++] = (char)*pos;
        } else if (*pos >= 0x20) {
            buffer[used++] = (char)*pos;
        }
    }
    if (used < buffer_size) {
        buffer[used] = '\0';
    }
    return used;
}
static bool sample_ntc_ok(const sensor_sample_t *sample)
{
    return sample != NULL &&
           sample->ntc_valid[0] &&
           sample->ntc_valid[1] &&
           sample->ntc_valid[2];
}

static void apply_battery_state(overload_result_t *result, const sensor_sample_t *sample)
{
    if (result == NULL || sample == NULL) {
        return;
    }
    if (result->state == APP_STATE_NORMAL &&
        sample->battery_state != APP_BATTERY_NORMAL) {
        result->state = APP_STATE_LOW_BATTERY;
    }
}

static uint32_t sample_period_ms_from_battery(const sensor_sample_t *sample)
{
    if (sample == NULL) {
        return APP_SAMPLE_PERIOD_MS;
    }
    if (sample->battery_state == APP_BATTERY_CRITICAL) {
        return APP_CRITICAL_BATTERY_SAMPLE_PERIOD_MS;
    }
    if (sample->battery_state == APP_BATTERY_LOW) {
        return APP_LOW_BATTERY_SAMPLE_PERIOD_MS;
    }
    return APP_SAMPLE_PERIOD_MS;
}

static uint32_t upload_period_ms_from_battery(const sensor_sample_t *sample)
{
    if (sample == NULL) {
        return APP_TELEMETRY_UPLOAD_PERIOD_MS;
    }
    if (sample->battery_state == APP_BATTERY_CRITICAL) {
        return APP_CRITICAL_BATTERY_UPLOAD_PERIOD_MS;
    }
    if (sample->battery_state == APP_BATTERY_LOW) {
        return APP_LOW_BATTERY_UPLOAD_PERIOD_MS;
    }
    return APP_TELEMETRY_UPLOAD_PERIOD_MS;
}

static uint32_t run_self_test(void)
{
    sensor_sample_t sample = {0};
    esp_err_t err = ntc_sampler_read(&sample);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "self test sensor read failed: %s", esp_err_to_name(err));
        return SELF_TEST_FAULT_SENSOR_READ;
    }

    uint32_t fault_mask = 0;
    if (!sample.ntc_valid[0]) {
        fault_mask |= SELF_TEST_FAULT_NTC1;
    }
    if (!sample.ntc_valid[1]) {
        fault_mask |= SELF_TEST_FAULT_NTC2;
    }
    if (!sample.ntc_valid[2]) {
        fault_mask |= SELF_TEST_FAULT_NTC_ENV;
    }
    if (!sample.battery_valid) {
        fault_mask |= SELF_TEST_FAULT_BATTERY;
    }

    ESP_LOGI(TAG,
             "self test %s, fault_mask=0x%08" PRIx32 ", ntc_ok=[%d,%d,%d], battery=%.2fV",
             fault_mask == 0 ? "ok" : "failed",
             fault_mask,
             sample.ntc_valid[0],
             sample.ntc_valid[1],
             sample.ntc_valid[2],
             sample.battery_voltage_v);
    return fault_mask;
}

static void handle_host_commands(runtime_stats_t *stats)
{
    app_command_t command = {0};
    while (command_udp_take(&command)) {
        switch (command.type) {
        case COMMAND_SELF_TEST: {
            ESP_LOGW(TAG, "host command: self_test");
            uint32_t fault_mask = run_self_test();
            runtime_stats_set_self_test(stats, fault_mask == 0, fault_mask);
            break;
        }
        case COMMAND_RESET_STATS:
            ESP_LOGW(TAG, "host command: reset_stats");
            runtime_stats_init(stats);
            runtime_stats_set_self_test(stats, true, 0);
            break;
        case COMMAND_WIFI_SCAN: {
            ESP_LOGW(TAG, "host command: wifi_scan");
            s_wifi_scan_ready = false;
            s_wifi_scan_count = APP_WIFI_SCAN_MAX_AP;
            strlcpy(s_wifi_scan_status, "scanning", sizeof(s_wifi_scan_status));
            s_wifi_scan_error[0] = '\0';
            esp_err_t scan_err = app_wifi_scan(s_wifi_scan_records, &s_wifi_scan_count);
            s_wifi_scan_ready = true;
            if (scan_err == ESP_OK) {
                strlcpy(s_wifi_scan_status, "ok", sizeof(s_wifi_scan_status));
                ESP_LOGI(TAG, "wifi scan completed, count:%u", s_wifi_scan_count);
            } else {
                s_wifi_scan_count = 0;
                strlcpy(s_wifi_scan_status, "error", sizeof(s_wifi_scan_status));
                strlcpy(s_wifi_scan_error, esp_err_to_name(scan_err), sizeof(s_wifi_scan_error));
                ESP_LOGE(TAG, "wifi scan failed: %s", s_wifi_scan_error);
            }
            break;
        }
        case COMMAND_WIFI_UPDATE:
            ESP_LOGW(TAG, "host command: wifi_update SSID:%s", command.ssid);
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                app_wifi_save_credentials_and_restart(command.ssid, command.password));
            break;
        case COMMAND_WIFI_CLEAR:
            ESP_LOGW(TAG, "host command: wifi_clear");
            ESP_ERROR_CHECK_WITHOUT_ABORT(app_wifi_clear_credentials_and_restart());
            break;
        case COMMAND_NONE:
        default:
            break;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "GraEsp terminal starting");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_ERROR_CHECK(device_identity_init());
    ESP_LOGI(TAG, "device_id:%s", device_identity_get());

    ESP_ERROR_CHECK(alarm_controller_init());

#if APP_BOARD_TEST_LED_BUZZER_ALTERNATE
    ESP_LOGW(TAG, "board test mode: LED and buzzer alternate every %d ms",
             APP_BOARD_TEST_ALTERNATE_PERIOD_MS);
    while (true) {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        alarm_controller_update(APP_STATE_NORMAL, now_ms);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#endif

    ESP_ERROR_CHECK(ntc_sampler_init());
    runtime_stats_t stats;
    runtime_stats_init(&stats);
    const uint32_t self_test_fault_mask = run_self_test();
    runtime_stats_set_self_test(&stats, self_test_fault_mask == 0, self_test_fault_mask);

    ESP_ERROR_CHECK(app_wifi_station_start());
    if (!app_wifi_station_wait_connected(APP_WIFI_CONNECT_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "primary WiFi not connected, trying built-in debug WiFi");
        esp_err_t debug_wifi_err = app_wifi_station_try_builtin_debug();
        if (debug_wifi_err == ESP_OK &&
            app_wifi_station_wait_connected(APP_WIFI_CONNECT_TIMEOUT_MS)) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(telemetry_udp_init());
        } else {
            ESP_LOGW(TAG, "WiFi not connected, starting SoftAP provisioning");
            ESP_ERROR_CHECK(app_wifi_provisioning_start());
        }
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(telemetry_udp_init());
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(command_udp_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_bridge_start());
    feature_extractor_reset();
    model_predictor_reset();
    overload_detector_reset();
    ESP_LOGI(TAG, "edge model: %s", model_predictor_version());
    int64_t last_wifi_recovery_ms = esp_timer_get_time() / 1000;
    int64_t last_telemetry_upload_ms = 0;

    while (true) {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        handle_host_commands(&stats);

        sensor_sample_t sample = {0};
        esp_err_t err = ntc_sampler_read(&sample);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "sensor read failed: %s", esp_err_to_name(err));
            alarm_controller_update(APP_STATE_FAULT, now_ms);
            vTaskDelay(pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
            continue;
        }

        thermal_features_t features = feature_extractor_update(&sample, now_ms);
        overload_result_t result = {0};
        if (!sample_ntc_ok(&sample)) {
            ESP_LOGW(TAG,
                     "sensor fault, ntc_ok=[%d,%d,%d], adc_mv=[%d,%d,%d]",
                     sample.ntc_valid[0],
                     sample.ntc_valid[1],
                     sample.ntc_valid[2],
                     sample.raw_mv[0],
                     sample.raw_mv[1],
                     sample.raw_mv[2]);
            result.state = APP_STATE_FAULT;
        } else {
#if APP_USE_EDGE_AI_MODEL
            model_prediction_t prediction = model_predictor_predict(&sample, &features);
            result = overload_detector_update_from_prediction(&features, &prediction);
#else
            float estimated_current_a = current_estimator_estimate_a(&features);
            result = overload_detector_update(&features, estimated_current_a);
#endif
            overload_detector_apply_state_machine(&result, &features, now_ms);
            apply_battery_state(&result, &sample);
        }

        const uint32_t sample_period_ms = sample_period_ms_from_battery(&sample);
        const uint32_t upload_period_ms = upload_period_ms_from_battery(&sample);

        runtime_stats_update(&stats, &features, &result);
        alarm_controller_update(result.state, now_ms);

        char json[2048];
        telemetry_json_build(json, sizeof(json), now_ms, &sample, &features, &result, &stats);
        if (s_wifi_scan_ready) {
            size_t used = strlen(json);
            if (used > 0 && json[used - 1] == '}') {
                used--;
            }
            used = json_appendf(json,
                                sizeof(json),
                                used,
                                ",\"wifi_scan_status\":\"%s\",\"wifi_scan_error\":\"%s\",\"wifi_scan\":[",
                                s_wifi_scan_status,
                                s_wifi_scan_error);
            for (uint16_t i = 0; i < s_wifi_scan_count && used < sizeof(json) - 8; i++) {
                used = json_appendf(json,
                                    sizeof(json),
                                    used,
                                    "%s{\"ssid\":\"",
                                    i == 0 ? "" : ",");
                used = json_append_escaped(json, sizeof(json), used, (const char *)s_wifi_scan_records[i].ssid);
                used = json_appendf(json,
                                    sizeof(json),
                                    used,
                                    "\",\"rssi\":%d}",
                                    s_wifi_scan_records[i].rssi);
            }
            (void)json_appendf(json, sizeof(json), used, "]}");
        }
        ESP_LOGI(TAG, "%s", json);
        if (app_wifi_station_is_connected()) {
            if ((now_ms - last_telemetry_upload_ms) >= upload_period_ms) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(telemetry_udp_send(json));
                ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_bridge_publish_json(json));
                last_telemetry_upload_ms = now_ms;
            }
        } else {
            if ((now_ms - last_wifi_recovery_ms) >= APP_WIFI_RECOVERY_CHECK_MS) {
                last_wifi_recovery_ms = now_ms;
                ESP_LOGW(TAG, "WiFi still offline, keeping SoftAP provisioning available");
                ESP_ERROR_CHECK_WITHOUT_ABORT(app_wifi_provisioning_start());
            }
        }

        vTaskDelay(pdMS_TO_TICKS(sample_period_ms));
    }
}






