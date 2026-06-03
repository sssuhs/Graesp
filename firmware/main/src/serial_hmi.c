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

    char line[320];
    int written = snprintf(line,
                           sizeof(line),
                           "GRAESP,HMI,device=%s,uptime=%" PRId64
                           ",state=%s,rise=%.2f,current=%.2f,prob=%.2f,battery=%d,"
                           "ntc1=%.2f,ntc2=%.2f,ambient=%.2f,rate=%.2f,self=%s,fault=0x%08" PRIx32 "\r\n",
                           device_identity_get(),
                           uptime_ms,
                           app_state_to_string(result->state),
                           features->temp_rise_c,
                           result->estimated_current_a,
                           result->overload_probability,
                           sample->battery_percent,
                           sample->ntc_temp_c[0],
                           sample->ntc_temp_c[1],
                           sample->ambient_temp_c,
                           features->heating_rate_c_per_min,
                           stats->self_test_ok ? "ok" : "fault",
                           stats->self_test_fault_mask);
    if (written <= 0) {
        return ESP_FAIL;
    }
    if (written >= (int)sizeof(line)) {
        written = (int)sizeof(line) - 1;
    }

    const int sent = uart_write_bytes(APP_SERIAL_HMI_UART_NUM, line, written);
    return sent == written ? ESP_OK : ESP_FAIL;
#else
    (void)uptime_ms;
    (void)sample;
    (void)features;
    (void)result;
    (void)stats;
    return ESP_OK;
#endif
}
