#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t device_identity_init(void);
const char *device_identity_get(void);
void device_identity_copy(char *buffer, size_t buffer_size);
