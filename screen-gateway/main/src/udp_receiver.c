#include "udp_receiver.h"

#include <stdio.h>
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

static const char *source_ip_string(const struct sockaddr_in *source_addr, char *buffer, size_t size)
{
    if (source_addr == NULL || buffer == NULL || size == 0) {
        return "";
    }
    inet_ntoa_r(source_addr->sin_addr, buffer, size);
    return buffer;
}

#if GATEWAY_DEBUG_ACK_ENABLE
static void send_debug_ack(int sock, const gateway_telemetry_t *telemetry, const char *source_ip)
{
    if (telemetry == NULL || source_ip == NULL) {
        return;
    }

    char ack[256];
    const int len = snprintf(ack,
                             sizeof(ack),
                             "{\"type\":\"gateway_rx\",\"device_id\":\"%s\",\"state\":\"%s\","
                             "\"source_ip\":\"%s\",\"rise\":%.2f,\"current\":%.2f,"
                             "\"prob\":%.2f,\"battery\":%d}",
                             telemetry->device_id,
                             telemetry->state,
                             source_ip,
                             telemetry->temp_rise_c,
                             telemetry->estimated_current_a,
                             telemetry->overload_probability,
                             telemetry->battery_percent);
    if (len <= 0) {
        return;
    }

    struct sockaddr_in ack_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(GATEWAY_DEBUG_ACK_UDP_PORT),
        .sin_addr.s_addr = inet_addr("255.255.255.255"),
    };
    const size_t send_len = (size_t)len < sizeof(ack) ? (size_t)len : sizeof(ack) - 1;
    sendto(sock, ack, send_len, 0, (struct sockaddr *)&ack_addr, sizeof(ack_addr));
}
#endif

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

#if GATEWAY_DEBUG_ACK_ENABLE
        const int broadcast_enable = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
#endif

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
#if GATEWAY_TJC_ENABLE
        ESP_ERROR_CHECK_WITHOUT_ABORT(tjc_screen_show_status("等待数据"));
#endif

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
                char source_ip[16];
                source_ip_string(&source_addr, source_ip, sizeof(source_ip));
                ESP_LOGI(TAG,
                         "rx %dB from %s device=%s state=%s rise=%.2fC current=%.2fA prob=%.2f battery=%d%%",
                         len,
                         source_ip,
                         telemetry.device_id,
                         telemetry.state,
                         telemetry.temp_rise_c,
                         telemetry.estimated_current_a,
                         telemetry.overload_probability,
                         telemetry.battery_percent);
#if GATEWAY_DEBUG_ACK_ENABLE
                send_debug_ack(sock, &telemetry, source_ip);
#endif
#if GATEWAY_TJC_ENABLE
                ESP_ERROR_CHECK_WITHOUT_ABORT(tjc_screen_update(&telemetry));
#endif
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
