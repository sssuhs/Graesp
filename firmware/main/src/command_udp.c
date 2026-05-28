#include "command_udp.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "device_identity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "command_udp";

static QueueHandle_t s_command_queue;
static bool s_started;

static const char *skip_spaces(const char *text)
{
    while (text != NULL && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')) {
        text++;
    }
    return text;
}

static bool json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    if (json == NULL || key == NULL || out == NULL || out_size == 0) {
        return false;
    }

    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos + strlen(pattern), ':');
    if (pos == NULL) {
        return false;
    }
    pos = skip_spaces(pos + 1);
    if (pos == NULL || *pos != '"') {
        return false;
    }
    pos++;

    size_t index = 0;
    while (*pos != '\0' && *pos != '"' && index + 1 < out_size) {
        if (*pos == '\\' && pos[1] != '\0') {
            pos++;
        }
        out[index++] = *pos++;
    }
    out[index] = '\0';
    return index > 0;
}

static bool command_targets_this_device(const char *json)
{
    char target[40] = {0};
    if (!json_get_string(json, "device_id", target, sizeof(target))) {
        return true;
    }
    return strcmp(target, "broadcast") == 0 || strcmp(target, device_identity_get()) == 0;
}

static bool parse_command(const char *json, app_command_t *out_command)
{
    char command[24] = {0};
    if (!json_get_string(json, "command", command, sizeof(command))) {
        return false;
    }
    if (!command_targets_this_device(json)) {
        return false;
    }

    memset(out_command, 0, sizeof(*out_command));
    if (strcmp(command, "self_test") == 0) {
        out_command->type = COMMAND_SELF_TEST;
        return true;
    }
    if (strcmp(command, "reset_stats") == 0) {
        out_command->type = COMMAND_RESET_STATS;
        return true;
    }
    if (strcmp(command, "wifi_scan") == 0) {
        out_command->type = COMMAND_WIFI_SCAN;
        return true;
    }
    if (strcmp(command, "wifi_update") == 0) {
        if (!json_get_string(json, "ssid", out_command->ssid, sizeof(out_command->ssid))) {
            return false;
        }
        (void)json_get_string(json, "password", out_command->password, sizeof(out_command->password));
        out_command->type = COMMAND_WIFI_UPDATE;
        return true;
    }
    if (strcmp(command, "wifi_clear") == 0) {
        out_command->type = COMMAND_WIFI_CLEAR;
        return true;
    }

    return false;
}

static void command_udp_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "create UDP command socket failed, errno:%d", errno);
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in listen_addr = {0};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(APP_COMMAND_UDP_PORT);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "bind UDP command port failed, errno:%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP command listening on port %d", APP_COMMAND_UDP_PORT);
    while (true) {
        char buffer[256] = {0};
        struct sockaddr_in source_addr = {0};
        socklen_t source_len = sizeof(source_addr);
        int len = recvfrom(sock,
                           buffer,
                           sizeof(buffer) - 1,
                           0,
                           (struct sockaddr *)&source_addr,
                           &source_len);
        if (len <= 0) {
            continue;
        }
        buffer[len] = '\0';

        app_command_t command = {0};
        if (parse_command(buffer, &command)) {
            ESP_LOGI(TAG, "received command type:%d", command.type);
            if (!command_post(&command)) {
                ESP_LOGW(TAG, "command queue full, dropping command");
            }
        }
    }
}

esp_err_t command_udp_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_command_queue = xQueueCreate(4, sizeof(app_command_t));
    if (s_command_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(command_udp_task, "command_udp", 4096, NULL, 5, NULL);
    if (ok != pdPASS) {
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

bool command_post(const app_command_t *command)
{
    if (s_command_queue == NULL || command == NULL) {
        return false;
    }
    return xQueueSend(s_command_queue, command, 0) == pdTRUE;
}
bool command_udp_take(app_command_t *out_command)
{
    if (s_command_queue == NULL || out_command == NULL) {
        return false;
    }
    return xQueueReceive(s_command_queue, out_command, 0) == pdTRUE;
}

