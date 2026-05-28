#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum {
    COMMAND_NONE = 0,
    COMMAND_SELF_TEST,
    COMMAND_RESET_STATS,
    COMMAND_WIFI_SCAN,
    COMMAND_WIFI_UPDATE,
    COMMAND_WIFI_CLEAR,
} command_type_t;

typedef struct {
    command_type_t type;
    char ssid[33];
    char password[65];
} app_command_t;

esp_err_t command_udp_start(void);
bool command_post(const app_command_t *command);
bool command_udp_take(app_command_t *out_command);


