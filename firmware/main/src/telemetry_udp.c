#include "telemetry_udp.h"

#include <errno.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "telemetry_udp";

static int s_socket = -1;
static struct sockaddr_in s_destination;

esp_err_t telemetry_udp_init(void)
{
    if (s_socket >= 0) {
        return ESP_OK;
    }

    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_socket < 0) {
        ESP_LOGE(TAG, "create UDP socket failed, errno:%d", errno);
        return ESP_FAIL;
    }

    int broadcast = 1;
    if (setsockopt(s_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "enable broadcast failed, errno:%d", errno);
        close(s_socket);
        s_socket = -1;
        return ESP_FAIL;
    }

    memset(&s_destination, 0, sizeof(s_destination));
    s_destination.sin_family = AF_INET;
    s_destination.sin_port = htons(APP_TELEMETRY_UDP_PORT);
    s_destination.sin_addr.s_addr = inet_addr(APP_TELEMETRY_UDP_BROADCAST);

    ESP_LOGI(TAG, "UDP telemetry target %s:%d",
             APP_TELEMETRY_UDP_BROADCAST,
             APP_TELEMETRY_UDP_PORT);
    return ESP_OK;
}

esp_err_t telemetry_udp_send(const char *payload)
{
    if (payload == NULL || payload[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = telemetry_udp_init();
    if (err != ESP_OK) {
        return err;
    }

    const int sent = sendto(s_socket,
                            payload,
                            strlen(payload),
                            0,
                            (const struct sockaddr *)&s_destination,
                            sizeof(s_destination));
    if (sent < 0) {
        ESP_LOGW(TAG, "send UDP telemetry failed, errno:%d", errno);
        close(s_socket);
        s_socket = -1;
        return ESP_FAIL;
    }

    return ESP_OK;
}

