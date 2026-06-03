#include "esp_log.h"
#include "nvs_flash.h"

#include "gateway_config.h"

static const char *TAG = "graesp_screen_gateway";

void app_main(void)
{
    ESP_LOGI(TAG, "GraEsp screen gateway starting");
    ESP_LOGI(TAG, "target screen: %s", GATEWAY_TJC_MODEL);

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    ESP_LOGI(TAG, "gateway skeleton ready; WiFi receiver and TJC UART output are next");
}
