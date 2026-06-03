#include "tjc_screen.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "gateway_config.h"

static esp_err_t send_command(const char *command)
{
    if (command == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t len = strlen(command);
    int sent = uart_write_bytes(GATEWAY_TJC_UART_NUM, command, len);
    if (sent != (int)len) {
        return ESP_FAIL;
    }

    const uint8_t end[3] = {0xff, 0xff, 0xff};
    sent = uart_write_bytes(GATEWAY_TJC_UART_NUM, end, sizeof(end));
    return sent == (int)sizeof(end) ? ESP_OK : ESP_FAIL;
}

static esp_err_t set_text(const char *control, const char *text)
{
    char command[160];
    snprintf(command,
             sizeof(command),
             "%s.%s.txt=\"%s\"",
             GATEWAY_TJC_PAGE,
             control,
             text != NULL ? text : "");
    return send_command(command);
}

static esp_err_t set_value(const char *control, int value)
{
    char command[96];
    snprintf(command,
             sizeof(command),
             "%s.%s.val=%d",
             GATEWAY_TJC_PAGE,
             control,
             value);
    return send_command(command);
}

static int clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static const char *state_text(const char *state)
{
    if (state == NULL) {
        return "--";
    }
    if (strcmp(state, "normal") == 0) {
        return "正常";
    }
    if (strcmp(state, "temp_high") == 0) {
        return "温升偏高";
    }
    if (strcmp(state, "warning") == 0) {
        return "预警";
    }
    if (strcmp(state, "overload") == 0) {
        return "过载";
    }
    if (strcmp(state, "low_battery") == 0) {
        return "低电量";
    }
    if (strcmp(state, "fault") == 0) {
        return "故障";
    }
    return state;
}

esp_err_t tjc_screen_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = GATEWAY_TJC_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(GATEWAY_TJC_UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GATEWAY_TJC_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(GATEWAY_TJC_UART_NUM,
                                 GATEWAY_TJC_TX_GPIO,
                                 GATEWAY_TJC_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_command("bkcmd=1"));
    return tjc_screen_show_status("等待数据");
}

esp_err_t tjc_screen_show_status(const char *status_text)
{
    return set_text("t_state", status_text);
}

esp_err_t tjc_screen_update(const gateway_telemetry_t *telemetry)
{
    if (telemetry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char text[48];
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_device", telemetry->device_id));
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_state", state_text(telemetry->state)));

    snprintf(text, sizeof(text), "%.2f C", telemetry->temp_rise_c);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_rise", text));

    snprintf(text, sizeof(text), "%.2f A", telemetry->estimated_current_a);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_current", text));

    const int prob_percent = clamp_percent((int)(telemetry->overload_probability * 100.0f + 0.5f));
    snprintf(text, sizeof(text), "%d%%", prob_percent);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_prob", text));
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_value("j_prob", prob_percent));

    const int battery = clamp_percent(telemetry->battery_percent);
    snprintf(text, sizeof(text), "%d%%", battery);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_battery", text));
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_value("j_battery", battery));

    snprintf(text, sizeof(text), "%.2f C", telemetry->ntc1_c);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_ntc1", text));

    snprintf(text, sizeof(text), "%.2f C", telemetry->ntc2_c);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_ntc2", text));

    snprintf(text, sizeof(text), "%.2f C", telemetry->ambient_c);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_ambient", text));

    snprintf(text, sizeof(text), "%.2f C/min", telemetry->heating_rate_c_per_min);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_rate", text));

    snprintf(text,
             sizeof(text),
             "%s 0x%08lx",
             telemetry->self_test_ok ? "OK" : "FAULT",
             telemetry->self_test_fault_mask);
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_text("t_fault", text));
    return ESP_OK;
}
