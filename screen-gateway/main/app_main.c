#include "esp_log.h"
#include "nvs_flash.h"

#include "gateway_config.h"
#include "tjc_screen.h"
#include "udp_receiver.h"
#include "wifi_station.h"

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_NONE);

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    ESP_ERROR_CHECK(tjc_screen_init());
    ESP_ERROR_CHECK(tjc_screen_show_status("连接WiFi"));

    ESP_ERROR_CHECK(gateway_wifi_start());
    if (gateway_wifi_wait_connected(15000)) {
        ESP_ERROR_CHECK(tjc_screen_show_status("等待数据"));
    } else {
        ESP_ERROR_CHECK(tjc_screen_show_status("WiFi未连"));
    }

    ESP_ERROR_CHECK(udp_receiver_start());
}
