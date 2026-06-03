#include "serial_hmi.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "device_identity.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "overload_detector.h"

static const char *TAG = "serial_hmi";
static bool s_boot_sent;

static esp_err_t serial_hmi_send_tjc_command(const char *command)
{
    if (command == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t len = strlen(command);
    int sent = uart_write_bytes(APP_SERIAL_HMI_UART_NUM, command, len);
    if (sent != (int)len) {
        return ESP_FAIL;
    }

    const uint8_t end[3] = {0xff, 0xff, 0xff};
    sent = uart_write_bytes(APP_SERIAL_HMI_UART_NUM, end, sizeof(end));
    return sent == (int)sizeof(end) ? ESP_OK : ESP_FAIL;
}

static esp_err_t serial_hmi_set_text(const char *control, const char *text)
{
    char command[160];
    snprintf(command,
             sizeof(command),
             "%s.%s.txt=\"%s\"",
             APP_SERIAL_HMI_TJC_PAGE,
             control,
             text != NULL ? text : "");
    return serial_hmi_send_tjc_command(command);
}

static esp_err_t serial_hmi_set_value(const char *control, int value)
{
    char command[96];
    snprintf(command,
             sizeof(command),
             "%s.%s.val=%d",
             APP_SERIAL_HMI_TJC_PAGE,
             control,
             value);
    return serial_hmi_send_tjc_command(command);
}

static const char *state_to_cn(app_state_t state)
{
    switch (state) {
    case APP_STATE_NORMAL:
        return "正常";
    case APP_STATE_TEMP_HIGH:
        return "温升偏高";
    case APP_STATE_WARNING:
        return "预警";
    case APP_STATE_OVERLOAD:
        return "过载";
    case APP_STATE_LOW_BATTERY:
        return "低电量";
    case APP_STATE_FAULT:
    default:
        return "故障";
    }
}

esp_err_t serial_hmi_init(void)
{
#if APP_SERIAL_HMI_ENABLE
    const uart_config_t uart_config = {
        .baud_rate = APP_SERIAL_HMI_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(APP_SERIAL_HMI_UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(APP_SERIAL_HMI_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(APP_SERIAL_HMI_UART_NUM,
                                 APP_SERIAL_HMI_TX_GPIO,
                                 APP_SERIAL_HMI_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG,
             "serial HMI UART%d enabled, tx:%d rx:%d baud:%d",
             APP_SERIAL_HMI_UART_NUM,
             APP_SERIAL_HMI_TX_GPIO,
             APP_SERIAL_HMI_RX_GPIO,
             APP_SERIAL_HMI_BAUDRATE);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_send_tjc_command("bkcmd=1"));
#endif
    return ESP_OK;
}

esp_err_t serial_hmi_send_status(int64_t uptime_ms,
                                 const sensor_sample_t *sample,
                                 const thermal_features_t *features,
                                 const overload_result_t *result,
                                 const runtime_stats_t *stats)
{
#if APP_SERIAL_HMI_ENABLE
    if (sample == NULL || features == NULL || result == NULL || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)uptime_ms;

    char text[48];
    esp_err_t err = ESP_OK;

    if (!s_boot_sent) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_send_tjc_command("bkcmd=1"));
        ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_device", device_identity_get()));
        s_boot_sent = true;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_state", state_to_cn(result->state)));

    snprintf(text, sizeof(text), "%.2f C", features->temp_rise_c);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_rise", text));

    snprintf(text, sizeof(text), "%.2f A", result->estimated_current_a);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_current", text));

    const int probability_percent = (int)(result->overload_probability * 100.0f + 0.5f);
    snprintf(text, sizeof(text), "%d%%", probability_percent);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_prob", text));
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_value("j_prob", probability_percent));

    snprintf(text, sizeof(text), "%d%%", sample->battery_percent);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_battery", text));
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_value("j_battery", sample->battery_percent));

    snprintf(text, sizeof(text), "%.2f C", sample->ntc_temp_c[0]);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_ntc1", text));

    snprintf(text, sizeof(text), "%.2f C", sample->ntc_temp_c[1]);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_ntc2", text));

    snprintf(text, sizeof(text), "%.2f C", sample->ambient_temp_c);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_ambient", text));

    snprintf(text, sizeof(text), "%.2f C/min", features->heating_rate_c_per_min);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_rate", text));

    snprintf(text,
             sizeof(text),
             "%s 0x%08" PRIx32,
             stats->self_test_ok ? "OK" : "FAULT",
             stats->self_test_fault_mask);
    ESP_ERROR_CHECK_WITHOUT_ABORT(serial_hmi_set_text("t_fault", text));

    return err;
#else
    (void)uptime_ms;
    (void)sample;
    (void)features;
    (void)result;
    (void)stats;
    return ESP_OK;
#endif
}
