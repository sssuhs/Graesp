#include "alarm_controller.h"

#include <stdbool.h>

#include "app_config.h"
#include "driver/gpio.h"

esp_err_t alarm_controller_init(void)
{
    gpio_set_level(APP_BUZZER_GPIO, APP_BUZZER_INACTIVE_LEVEL);

    gpio_config_t buzzer_conf = {
        .pin_bit_mask = 1ULL << APP_BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&buzzer_conf);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(APP_BUZZER_GPIO, APP_BUZZER_INACTIVE_LEVEL);

    gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << APP_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    err = gpio_config(&led_conf);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(APP_LED_GPIO, APP_BOARD_TEST_LED_ALWAYS_ON ? 1 : 0);
    return ESP_OK;
}

void alarm_controller_update(app_state_t state, int64_t now_ms)
{
#if APP_BOARD_TEST_LED_ALWAYS_ON
    (void)state;
    (void)now_ms;
    gpio_set_level(APP_LED_GPIO, 1);
    gpio_set_level(APP_BUZZER_GPIO, APP_BUZZER_INACTIVE_LEVEL);
    return;
#endif

#if APP_BOARD_TEST_LED_BUZZER_ALTERNATE
    (void)state;
    const bool led_on = ((now_ms / APP_BOARD_TEST_ALTERNATE_PERIOD_MS) % 2) == 0;
    gpio_set_level(APP_LED_GPIO, led_on ? 1 : 0);
    gpio_set_level(APP_BUZZER_GPIO, led_on ? APP_BUZZER_INACTIVE_LEVEL : APP_BUZZER_ACTIVE_LEVEL);
    return;
#endif

    int led = 0;
    int buzzer = 0;

    switch (state) {
    case APP_STATE_NORMAL:
        led = 0;
        buzzer = APP_BUZZER_INACTIVE_LEVEL;
        break;
    case APP_STATE_TEMP_HIGH:
        led = ((now_ms / 1000) % 2) == 0;
        buzzer = APP_BUZZER_INACTIVE_LEVEL;
        break;
    case APP_STATE_WARNING:
        led = ((now_ms / 500) % 2) == 0;
        buzzer = ((now_ms / 2000) % 2) == 0 ? APP_BUZZER_ACTIVE_LEVEL : APP_BUZZER_INACTIVE_LEVEL;
        break;
    case APP_STATE_OVERLOAD:
        led = ((now_ms / 200) % 2) == 0;
        buzzer = ((now_ms / 300) % 2) == 0 ? APP_BUZZER_ACTIVE_LEVEL : APP_BUZZER_INACTIVE_LEVEL;
        break;
    case APP_STATE_LOW_BATTERY:
        led = ((now_ms / 1500) % 2) == 0;
        buzzer = APP_BUZZER_INACTIVE_LEVEL;
        break;
    case APP_STATE_FAULT:
    default:
        led = ((now_ms / 1000) % 2) == 0;
        buzzer = APP_BUZZER_INACTIVE_LEVEL;
        break;
    }

    gpio_set_level(APP_LED_GPIO, led);
    gpio_set_level(APP_BUZZER_GPIO, buzzer);
}
