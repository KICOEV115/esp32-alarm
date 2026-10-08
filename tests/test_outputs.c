#include "panel_outputs.h"
#include "board_config.h"
#include "driver/gpio.h"
#include <assert.h>
#include <stdio.h>

static int level = -1;
static unsigned writes;
static bool fail_write;
static bool fail_config;

esp_err_t gpio_config(const gpio_config_t *config)
{
    assert(config->pin_bit_mask == (UINT64_C(1) << 25));
    assert(config->mode == GPIO_MODE_OUTPUT);
    assert(level == 0);
    return fail_config ? ESP_FAIL : ESP_OK;
}

esp_err_t gpio_set_level(int pin, uint32_t next_level)
{
    assert(pin == 25);
    assert(next_level <= 1);
    ++writes;
    if (fail_write) {
        return ESP_FAIL;
    }
    level = (int)next_level;
    return ESP_OK;
}

int main(void)
{
    assert(ALARM_LED_GPIO == 25);
    fail_write = true;
    assert(panel_outputs_start() == ESP_FAIL);
    fail_write = false;
    fail_config = true;
    assert(panel_outputs_start() == ESP_FAIL);
    fail_config = false;
    assert(panel_outputs_start() == ESP_OK);
    assert(level == 0);

    alarm_panel_t panel;
    alarm_panel_init(&panel);
    panel_outputs_update(&panel);
    assert(level == 0);
    alarm_message_t state = {
        .type = ALARM_STATE,
        .role = ALARM_DETECTOR,
        .session = 1,
        .sequence = 1,
        .flags = ALARM_READY,
    };
    alarm_panel_report(&panel, &state, 0);
    alarm_panel_set_link(&panel, true);
    assert(alarm_panel_arm(&panel, 0));
    panel_outputs_update(&panel);
    assert(level == 0);
    alarm_panel_tick(&panel, ALARM_EXIT_MS);
    panel_outputs_update(&panel);
    assert(level == 0);

    state.flags |= ALARM_HIGH;
    state.motion_count = 1;
    alarm_panel_report(&panel, &state, ALARM_EXIT_MS + 1);
    assert(panel.mode == PANEL_ALARM);
    fail_write = true;
    panel_outputs_update(&panel);
    assert(level == 0);
    fail_write = false;
    panel_outputs_update(&panel);
    assert(level == 1);

    unsigned before = writes;
    state.flags = ALARM_READY;
    alarm_panel_report(&panel, &state, ALARM_EXIT_MS + 2);
    alarm_panel_set_link(&panel, false);
    panel_outputs_update(&panel);
    assert(level == 1 && writes == before);

    alarm_panel_disarm(&panel);
    fail_write = true;
    panel_outputs_update(&panel);
    assert(level == 1);
    fail_write = false;
    panel_outputs_update(&panel);
    assert(level == 0);
    panel.fault = FAULT_RESTART;
    panel_outputs_update(&panel);
    assert(level == 0);
    puts("PASS LED: startup off, arming/armed off, motion on, CLEAR/link-loss latch, disarm off, "
         "failed writes retried");
    return 0;
}
