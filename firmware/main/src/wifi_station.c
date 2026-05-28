#include "wifi_station.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_MAX_RETRY 10

static const char *TAG = "wifi_station";

static EventGroupHandle_t s_wifi_event_group;
static httpd_handle_t s_http_server;
static bool s_started;
static bool s_connected;
static bool s_ap_started;
static bool s_using_builtin_debug_wifi;
static int s_retry_count;
static char s_current_ssid[33];

static const char *PROV_HTML =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>GraEsp WiFi</title>"
    "<style>body{font-family:Arial,sans-serif;margin:28px;max-width:520px}"
    "input,button{box-sizing:border-box;width:100%;padding:12px;margin:8px 0;font-size:16px}"
    "button{background:#1267d6;color:white;border:0;border-radius:4px}"
    ".danger{background:#9f1239}</style></head>"
    "<body><h2>GraEsp WiFi Setup</h2>"
    "<form method=\"post\" action=\"/save\">"
    "<input name=\"ssid\" placeholder=\"WiFi SSID\" maxlength=\"32\" required>"
    "<input name=\"password\" placeholder=\"WiFi Password\" maxlength=\"63\" type=\"password\">"
    "<button type=\"submit\">Save and Restart</button></form>"
    "<form method=\"post\" action=\"/clear\">"
    "<button class=\"danger\" type=\"submit\">Clear Saved WiFi</button></form>"
    "<p>Debug fallback WiFi: TT24 / 88888888.</p></body></html>";

static bool nvs_read_string(nvs_handle_t handle, const char *key, char *out, size_t out_size)
{
    size_t len = out_size;
    esp_err_t err = nvs_get_str(handle, key, out, &len);
    return err == ESP_OK && out[0] != '\0';
}

static bool load_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    ssid[0] = '\0';
    password[0] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        bool ok = nvs_read_string(handle, "ssid", ssid, ssid_size);
        if (ok) {
            size_t len = password_size;
            err = nvs_get_str(handle, "password", password, &len);
            if (err != ESP_OK) {
                password[0] = '\0';
            }
        }
        nvs_close(handle);
        if (ok) {
            ESP_LOGI(TAG, "loaded WiFi credentials from NVS, SSID:%s", ssid);
            return true;
        }
    }

    ESP_LOGW(TAG, "no saved WiFi credentials");
    return false;
}

static void load_builtin_debug_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    strlcpy(ssid, APP_WIFI_SSID, ssid_size);
    strlcpy(password, APP_WIFI_PASSWORD, password_size);
}

static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle),
                        TAG,
                        "open WiFi NVS failed");
    esp_err_t err = nvs_set_str(handle, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "password", password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t clear_wifi_credentials(void)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle),
                        TAG,
                        "open WiFi NVS failed");
    esp_err_t err_ssid = nvs_erase_key(handle, "ssid");
    esp_err_t err_password = nvs_erase_key(handle, "password");
    esp_err_t err = nvs_commit(handle);
    nvs_close(handle);

    if (err_ssid != ESP_OK && err_ssid != ESP_ERR_NVS_NOT_FOUND) {
        return err_ssid;
    }
    if (err_password != ESP_OK && err_password != ESP_ERR_NVS_NOT_FOUND) {
        return err_password;
    }
    return err;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static void url_decode(char *text)
{
    char *read = text;
    char *write = text;
    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (*read == '%' && isxdigit((unsigned char)read[1]) && isxdigit((unsigned char)read[2])) {
            int hi = hex_value(read[1]);
            int lo = hex_value(read[2]);
            *write++ = (char)((hi << 4) | lo);
            read += 3;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_size)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "%s=", key);
    const char *start = strstr(body, pattern);
    if (start == NULL) {
        return false;
    }
    start += strlen(pattern);
    const char *end = strchr(start, '&');
    size_t len = end == NULL ? strlen(start) : (size_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1U;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    url_decode(out);
    return out[0] != '\0';
}

static void delayed_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PROV_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    wifi_ap_record_t records[APP_WIFI_SCAN_MAX_AP] = {0};
    uint16_t count = APP_WIFI_SCAN_MAX_AP;
    esp_err_t err = app_wifi_scan(records, &count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan failed");
        return ESP_OK;
    }

    char body[1024];
    int offset = snprintf(body, sizeof(body), "{\"aps\":[");
    for (uint16_t i = 0; i < count && offset < (int)sizeof(body); i++) {
        offset += snprintf(body + offset,
                           sizeof(body) - offset,
                           "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
                           i == 0 ? "" : ",",
                           (const char *)records[i].ssid,
                           records[i].rssi,
                           records[i].authmode);
    }
    snprintf(body + offset, sizeof(body) - offset, "]}");

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[192] = {0};
    int total = 0;
    while (total < req->content_len && total < (int)sizeof(body) - 1) {
        int received = httpd_req_recv(req, body + total, sizeof(body) - 1 - total);
        if (received <= 0) {
            return ESP_FAIL;
        }
        total += received;
    }
    body[total] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};
    if (!form_get_value(body, "ssid", ssid, sizeof(ssid))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_OK;
    }
    (void)form_get_value(body, "password", password, sizeof(password));

    esp_err_t err = save_wifi_credentials(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save WiFi credentials failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "saved WiFi credentials, SSID:%s, restarting", ssid);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, "<html><body><h3>Saved. Restarting...</h3></body></html>");
    xTaskCreate(delayed_restart_task, "prov_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t clear_post_handler(httpd_req_t *req)
{
    esp_err_t err = clear_wifi_credentials();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear WiFi credentials failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "clear failed");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "cleared saved WiFi credentials, restarting");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, "<html><body><h3>Cleared. Restarting...</h3></body></html>");
    xTaskCreate(delayed_restart_task, "prov_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    if (s_http_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    ESP_RETURN_ON_ERROR(httpd_start(&s_http_server, &config), TAG, "start HTTP server failed");

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t clear = {
        .uri = "/clear",
        .method = HTTP_POST,
        .handler = clear_post_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t scan = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &root), TAG, "register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &save), TAG, "register save failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &clear), TAG, "register clear failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &scan), TAG, "register scan failed");
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "connecting to WiFi SSID:%s", s_current_ssid);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_retry_count++;
        ESP_LOGW(TAG, "WiFi disconnected, retry %d", s_retry_count);
        if (s_retry_count <= WIFI_MAX_RETRY) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "phone/pc joined provisioning AP, aid:%d", event->aid);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        s_retry_count = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi connected, IP:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t wifi_station_apply_credentials(const char *ssid, const char *password, bool builtin_debug)
{
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;

    strlcpy(s_current_ssid, ssid, sizeof(s_current_ssid));
    s_using_builtin_debug_wifi = builtin_debug;
    s_connected = false;
    s_retry_count = 0;
    if (s_wifi_event_group != NULL) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
    ESP_LOGI(TAG, "WiFi target selected: %s%s", ssid, builtin_debug ? " (built-in debug)" : " (provisioned)");
    return ESP_OK;
}

static void wifi_apply_power_save(void)
{
#if APP_WIFI_MODEM_SLEEP_ENABLE
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi modem-sleep power save enabled");
    } else {
        ESP_LOGW(TAG, "enable WiFi modem-sleep failed: %s", esp_err_to_name(err));
    }
#endif
}

esp_err_t app_wifi_station_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "create event group failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "create default event loop failed");
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "esp_wifi_set_storage failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                           ESP_EVENT_ANY_ID,
                                                           wifi_event_handler,
                                                           NULL,
                                                           NULL),
                        TAG,
                        "register WiFi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                           IP_EVENT_STA_GOT_IP,
                                                           wifi_event_handler,
                                                           NULL,
                                                           NULL),
                        TAG,
                        "register IP handler failed");

    char ssid[33] = {0};
    char password[65] = {0};
    if (!load_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        load_builtin_debug_credentials(ssid, sizeof(ssid), password, sizeof(password));
        ESP_LOGW(TAG, "using built-in debug WiFi SSID:%s", ssid);
    }

    ESP_RETURN_ON_ERROR(wifi_station_apply_credentials(ssid,
                                                       password,
                                                       strcmp(ssid, APP_WIFI_SSID) == 0 &&
                                                           strcmp(password, APP_WIFI_PASSWORD) == 0),
                        TAG,
                        "apply WiFi credentials failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
    wifi_apply_power_save();

    s_started = true;
    return ESP_OK;
}

esp_err_t app_wifi_station_try_builtin_debug(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_using_builtin_debug_wifi) {
        ESP_LOGW(TAG, "already using built-in debug WiFi SSID:%s", APP_WIFI_SSID);
        return ESP_ERR_INVALID_STATE;
    }

    char ssid[33] = {0};
    char password[65] = {0};
    load_builtin_debug_credentials(ssid, sizeof(ssid), password, sizeof(password));

    ESP_LOGW(TAG, "provisioned WiFi failed, trying built-in debug WiFi SSID:%s", ssid);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
    ESP_RETURN_ON_ERROR(wifi_station_apply_credentials(ssid, password, true),
                        TAG,
                        "apply built-in debug WiFi failed");
    wifi_apply_power_save();
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect built-in debug WiFi failed");
    return ESP_OK;
}

bool app_wifi_station_wait_connected(uint32_t timeout_ms)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool app_wifi_station_is_connected(void)
{
    return s_connected;
}

esp_err_t app_wifi_provisioning_start(void)
{
    if (s_ap_started) {
        return ESP_OK;
    }

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "read AP MAC failed");

    char ap_ssid[33] = {0};
    snprintf(ap_ssid, sizeof(ap_ssid), "%s-%02X%02X", APP_PROV_AP_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, APP_PROV_AP_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(ap_ssid);
    ap_config.ap.channel = 6;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = strlen(APP_PROV_AP_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set APSTA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set AP config failed");
    ESP_RETURN_ON_ERROR(start_http_server(), TAG, "start provisioning HTTP failed");

    s_ap_started = true;
    ESP_LOGW(TAG, "provisioning AP started: SSID:%s password:%s URL:http://192.168.4.1",
             ap_ssid,
             APP_PROV_AP_PASSWORD);
    return ESP_OK;
}

esp_err_t app_wifi_save_credentials_and_restart(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = save_wifi_credentials(ssid, password == NULL ? "" : password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save WiFi credentials failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGW(TAG, "WiFi credentials updated by host, SSID:%s, restarting", ssid);
    xTaskCreate(delayed_restart_task, "host_wifi_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t app_wifi_clear_credentials_and_restart(void)
{
    esp_err_t err = clear_wifi_credentials();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear WiFi credentials failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGW(TAG, "WiFi credentials cleared by host, restarting");
    xTaskCreate(delayed_restart_task, "host_wifi_clear", 2048, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t app_wifi_scan(wifi_ap_record_t *records, uint16_t *record_count)
{
    if (records == NULL || record_count == NULL || *record_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };

    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_config, true), TAG, "scan start failed");
    return esp_wifi_scan_get_ap_records(record_count, records);
}
