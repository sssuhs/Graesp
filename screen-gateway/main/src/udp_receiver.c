#include "udp_receiver.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_config.h"
#include "gateway_telemetry.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "tjc_screen.h"

static const char *TAG = "udp_receiver";

static void udp_receiver_task(void *arg)
{
    (void)arg;

    char rx_buffer[2048];

    while (true) {
        const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct sockaddr_in listen_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(GATEWAY_TELEMETRY_UDP_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };

        if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "listening UDP port %d", GATEWAY_TELEMETRY_UDP_PORT);
        ESP_ERROR_CHECK_WITHOUT_ABORT(tjc_screen_show_status("等待数据"));

        while (true) {
            struct sockaddr_in source_addr;
            socklen_t socklen = sizeof(source_addr);
            const int len = recvfrom(sock,
                                     rx_buffer,
                                     sizeof(rx_buffer) - 1,
                                     0,
                                     (struct sockaddr *)&source_addr,
                                     &socklen);
            if (len < 0) {
                break;
            }

            rx_buffer[len] = '\0';
            gateway_telemetry_t telemetry;
            if (gateway_telemetry_parse(rx_buffer, &telemetry)) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(tjc_screen_update(&telemetry));
            } else {
                ESP_LOGW(TAG, "invalid telemetry packet");
            }
        }

        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t udp_receiver_start(void)
{
    const BaseType_t ok = xTaskCreate(udp_receiver_task, "udp_receiver", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
