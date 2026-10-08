#include "panel_outputs.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "outputs";
static int applied_level = -1;
static bool write_error_reported;

esp_err_t panel_outputs_start(void)
{
    applied_level = -1;
    write_error_reported = false;

    /* Load the inactive level before enabling the output driver. */
    esp_err_t error = gpio_set_level(ALARM_LED_GPIO, 0);
    if (error != ESP_OK) {
        return error;
    }

    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << ALARM_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    error = gpio_config(&config);
    if (error != ESP_OK) {
        return error;
    }

    applied_level = 0;
    ESP_LOGI(TAG, "Alarm LED ready: GPIO%d, initially OFF", ALARM_LED_GPIO);
    return ESP_OK;
}

void panel_outputs_update(const alarm_panel_t *panel)
{
    const int desired_level = panel->mode == PANEL_ALARM;
    if (desired_level == applied_level) {
        return;
    }

    esp_err_t error = gpio_set_level(ALARM_LED_GPIO, desired_level);
    if (error != ESP_OK) {
        if (!write_error_reported) {
            ESP_LOGE(TAG, "LED write failed: %s; will retry", esp_err_to_name(error));
            write_error_reported = true;
        }
        /* Leave the old level intact so the next control pass retries. */
        return;
    }

    applied_level = desired_level;
    write_error_reported = false;
    ESP_LOGI(TAG, "ALARM LED %s", desired_level ? "ON" : "OFF");
}
