#include "mqtt_bridge.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "command_udp.h"
#include "device_identity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/apps/mqtt.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "overload_detector.h"

static const char *TAG = "mqtt_bridge";

static mqtt_client_t *s_client;
static bool s_started;
static bool s_connected;
static bool s_connecting;
static char s_client_id[48];
static char s_topic_telemetry[96];
static char s_topic_cmd[96];
static char s_topic_broadcast[96];
static char s_incoming_topic[96];
static char s_incoming_payload[256];
static size_t s_incoming_used;

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

static bool mqtt_parse_command(const char *json, app_command_t *out_command)
{
    char command[24] = {0};
    if (!json_get_string(json, "command", command, sizeof(command))) {
        if (!json_get_string(json, "cmd", command, sizeof(command))) {
            return false;
        }
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

static esp_err_t resolve_broker(ip_addr_t *out_ip)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result = NULL;
    int err = getaddrinfo(APP_MQTT_BROKER_HOST, NULL, &hints, &result);
    if (err != 0 || result == NULL) {
        ESP_LOGW(TAG, "resolve %s failed: %d", APP_MQTT_BROKER_HOST, err);
        return ESP_FAIL;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
    ip4_addr_t ip4;
    inet_addr_to_ip4addr(&ip4, &addr->sin_addr);
    ip_addr_copy_from_ip4(*out_ip, ip4);
    freeaddrinfo(result);
    return ESP_OK;
}

static void mqtt_request_cb(void *arg, err_t err)
{
    const char *name = (const char *)arg;
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "%s request failed: %d", name ? name : "mqtt", err);
    }
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t total_len)
{
    (void)arg;
    (void)total_len;
    s_incoming_used = 0;
    s_incoming_payload[0] = '\0';
    strlcpy(s_incoming_topic, topic ? topic : "", sizeof(s_incoming_topic));
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    (void)arg;
    if (data != NULL && len > 0 && s_incoming_used + len < sizeof(s_incoming_payload)) {
        memcpy(s_incoming_payload + s_incoming_used, data, len);
        s_incoming_used += len;
        s_incoming_payload[s_incoming_used] = '\0';
    }

    if ((flags & MQTT_DATA_FLAG_LAST) == 0) {
        return;
    }

    app_command_t command = {0};
    if (mqtt_parse_command(s_incoming_payload, &command)) {
        ESP_LOGI(TAG, "MQTT command topic:%s type:%d", s_incoming_topic, command.type);
        if (!command_post(&command)) {
            ESP_LOGW(TAG, "command queue full, dropping MQTT command");
        }
    } else {
        ESP_LOGW(TAG, "ignored MQTT payload: %s", s_incoming_payload);
    }
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    (void)arg;
    s_connecting = false;
    if (status == MQTT_CONNECT_ACCEPTED) {
        s_connected = true;
        ESP_LOGI(TAG, "connected to MQTT broker %s:%d", APP_MQTT_BROKER_HOST, APP_MQTT_BROKER_PORT);
        mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, NULL);
        (void)mqtt_subscribe(client, s_topic_cmd, 0, mqtt_request_cb, "subscribe device cmd");
        (void)mqtt_subscribe(client, s_topic_broadcast, 0, mqtt_request_cb, "subscribe broadcast cmd");
    } else {
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected/status:%d", status);
    }
}

static void mqtt_bridge_task(void *arg)
{
    (void)arg;

    while (true) {
        if (s_client == NULL) {
            s_client = mqtt_client_new();
            if (s_client == NULL) {
                ESP_LOGW(TAG, "create MQTT client failed");
                vTaskDelay(pdMS_TO_TICKS(APP_MQTT_RECONNECT_MS));
                continue;
            }
        }

        if (!s_connected && !s_connecting) {
            ip_addr_t broker_ip;
            if (resolve_broker(&broker_ip) == ESP_OK) {
                struct mqtt_connect_client_info_t info = {
                    .client_id = s_client_id,
                    .client_user = NULL,
                    .client_pass = NULL,
                    .keep_alive = APP_MQTT_KEEPALIVE_S,
                    .will_topic = NULL,
                    .will_msg = NULL,
                    .will_qos = 0,
                    .will_retain = 0,
                };
                ESP_LOGI(TAG, "connecting MQTT %s:%d as %s", APP_MQTT_BROKER_HOST, APP_MQTT_BROKER_PORT, s_client_id);
                err_t err = mqtt_client_connect(s_client, &broker_ip, APP_MQTT_BROKER_PORT, mqtt_connection_cb, NULL, &info);
                if (err == ERR_OK) {
                    s_connecting = true;
                } else {
                    ESP_LOGW(TAG, "mqtt_client_connect failed:%d", err);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(APP_MQTT_RECONNECT_MS));
    }
}

esp_err_t mqtt_bridge_start(void)
{
#if APP_MQTT_ENABLE
    if (s_started) {
        return ESP_OK;
    }

    const char *device_id = device_identity_get();
    snprintf(s_client_id, sizeof(s_client_id), "graesp-%s", device_id);
    snprintf(s_topic_telemetry, sizeof(s_topic_telemetry), "%s/%s/telemetry", APP_MQTT_BASE_TOPIC, device_id);
    snprintf(s_topic_cmd, sizeof(s_topic_cmd), "%s/%s/cmd", APP_MQTT_BASE_TOPIC, device_id);
    snprintf(s_topic_broadcast, sizeof(s_topic_broadcast), "%s/broadcast/cmd", APP_MQTT_BASE_TOPIC);

    BaseType_t ok = xTaskCreate(mqtt_bridge_task, "mqtt_bridge", 6144, NULL, 4, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "MQTT bridge enabled, telemetry:%s cmd:%s", s_topic_telemetry, s_topic_cmd);
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

bool mqtt_bridge_is_connected(void)
{
    return s_connected && s_client != NULL && mqtt_client_is_connected(s_client);
}
esp_err_t mqtt_bridge_publish_json(const char *payload)
{
#if APP_MQTT_ENABLE
    if (!mqtt_bridge_is_connected() || payload == NULL || payload[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t len = strlen(payload);
    if (len > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    err_t err = mqtt_publish(s_client, s_topic_telemetry, payload, (u16_t)len, 0, 0, mqtt_request_cb, "publish telemetry json");
    return err == ERR_OK ? ESP_OK : ESP_FAIL;
#else
    return ESP_OK;
#endif
}

esp_err_t mqtt_bridge_publish_status(const sensor_sample_t *sample,
                                     const thermal_features_t *features,
                                     const overload_result_t *result,
                                     const runtime_stats_t *stats)
{
#if APP_MQTT_ENABLE
    if (!mqtt_bridge_is_connected() || sample == NULL || features == NULL || result == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[512];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"device_id\":\"%s\",\"state\":\"%s\",\"ntc1\":%.2f,\"ntc2\":%.2f,\"ambient\":%.2f,\"rise\":%.2f,\"rate\":%.2f,\"current\":%.2f,\"prob\":%.2f,\"battery\":%d,\"self_test_ok\":%s,\"fault_mask\":%lu}",
                           device_identity_get(),
                           app_state_to_string(result->state),
                           sample->cable_temp_c[0],
                           sample->cable_temp_c[1],
                           sample->ambient_temp_c,
                           features->temp_rise_c,
                           features->heating_rate_c_per_min,
                           result->estimated_current_a,
                           result->overload_probability,
                           sample->battery_percent,
                           stats != NULL && stats->self_test_ok ? "true" : "false",
                           stats != NULL ? (unsigned long)stats->self_test_fault_mask : 0UL);
    if (written <= 0 || written >= (int)sizeof(payload)) {
        return ESP_ERR_NO_MEM;
    }

    err_t err = mqtt_publish(s_client, s_topic_telemetry, payload, (u16_t)strlen(payload), 0, 0, mqtt_request_cb, "publish telemetry");
    return err == ERR_OK ? ESP_OK : ESP_FAIL;
#else
    return ESP_OK;
#endif
}


