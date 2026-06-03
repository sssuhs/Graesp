#include "wifi_station.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "gateway_config.h"

static const char *TAG = "wifi_station";
static EventGroupHandle_t s_wifi_events;
static bool s_auto_connect_enabled = true;

#define WIFI_CONNECTED_BIT BIT0

static bool scan_has_test_wifi(void)
{
    wifi_ap_record_t records[8] = {0};
    uint16_t count = 8;
    uint8_t target_ssid[] = GATEWAY_WIFI_SSID;
    wifi_scan_config_t scan_config = {
        .ssid = target_ssid,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan test WiFi %s failed: %s", GATEWAY_WIFI_SSID, esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read test WiFi scan result failed: %s", esp_err_to_name(err));
        return false;
    }

    for (uint16_t i = 0; i < count; i++) {
        if (strcmp((const char *)records[i].ssid, GATEWAY_WIFI_SSID) == 0) {
            ESP_LOGW(TAG, "test WiFi %s found, using built-in credentials", GATEWAY_WIFI_SSID);
            return true;
        }
    }

    ESP_LOGW(TAG, "test WiFi %s not found, keeping built-in credentials and retrying", GATEWAY_WIFI_SSID);
    return false;
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_auto_connect_enabled) {
            ESP_LOGI(TAG, "connecting to WiFi SSID:%s", GATEWAY_WIFI_SSID);
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "STA started, delaying connect until test WiFi scan completes");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t gateway_wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, GATEWAY_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password,
            GATEWAY_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    s_auto_connect_enabled = false;
    ESP_ERROR_CHECK(esp_wifi_start());
    scan_has_test_wifi();
    s_auto_connect_enabled = true;
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "connecting to WiFi SSID:%s", GATEWAY_WIFI_SSID);
    return ESP_OK;
}

bool gateway_wifi_wait_connected(int timeout_ms)
{
    if (s_wifi_events == NULL) {
        return false;
    }

    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                                 WIFI_CONNECTED_BIT,
                                                 pdFALSE,
                                                 pdFALSE,
                                                 pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
