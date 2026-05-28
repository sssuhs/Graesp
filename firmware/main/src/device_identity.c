#include "device_identity.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_mac.h"

static const char *TAG = "device_identity";
static char s_device_id[24];

esp_err_t device_identity_init(void)
{
    if (s_device_id[0] != '\0') {
        return ESP_OK;
    }

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "read STA MAC failed");
    snprintf(s_device_id,
             sizeof(s_device_id),
             "%s-%02X%02X%02X",
             APP_DEVICE_ID_PREFIX,
             mac[3],
             mac[4],
             mac[5]);
    return ESP_OK;
}

const char *device_identity_get(void)
{
    if (s_device_id[0] == '\0') {
        (void)device_identity_init();
    }
    return s_device_id[0] == '\0' ? "GRAESP-UNKNOWN" : s_device_id;
}

void device_identity_copy(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    strlcpy(buffer, device_identity_get(), buffer_size);
}
