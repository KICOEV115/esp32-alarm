#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <inttypes.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "oled_config.h"
#include "oled_wire.h"
#include "panel_display.h"

static const char *TAG = "oled";
static QueueHandle_t state_mailbox;
/* Application task owns this publication filter; OLED task receives copies. */
static ui_state_t published_state;
static bool have_published;

static esp_err_t commands(i2c_master_dev_handle_t device, const uint8_t *bytes, size_t size)
{
    uint8_t packet[40] = {0x00}; /* Co=0, D/C=0: command stream. */
    if (size > sizeof(packet) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(packet + 1, bytes, size);
    return i2c_master_transmit(device, packet, size + 1, 200);
}

static int write_packet(void *context, const uint8_t *bytes, size_t size)
{
    return i2c_master_transmit((i2c_master_dev_handle_t)context, bytes, size, 200);
}

static esp_err_t flush_frame(i2c_master_dev_handle_t device, const uint8_t *frame,
                             uint8_t dirty_pages)
{
    return oled_wire_write_pages(write_packet, device, frame, dirty_pages, OLED_COLUMN_OFFSET);
}

static esp_err_t initialize_screen(i2c_master_dev_handle_t device)
{
#if OLED_USE_SH1106
    const uint8_t setup[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40, 0xad, 0x8b, 0xa1,
        0xc8, 0xda, 0x12, 0x81, 0x7f, 0xd9, 0x22, 0xdb, 0x35, 0xa4, 0xa6,
    };
#else
    const uint8_t setup[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40, 0x8d, 0x14, 0x20, 0x02, 0xa1,
        0xc8, 0xda, 0x12, 0x81, 0x7f, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0x2e,
    };
#endif
    esp_err_t error = commands(device, setup, sizeof(setup));
    if (error != ESP_OK) {
        return error;
    }
    static const uint8_t blank[UI_FRAME_BYTES] = {0};
    error = flush_frame(device, blank, UINT8_C(0xff));
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t turn_on[] = {0xaf};
    return commands(device, turn_on, sizeof(turn_on));
}

static void display_task(void *argument)
{
    (void)argument;
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t configuration = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t error = i2c_new_master_bus(&configuration, &bus);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(error));
        vTaskDelete(NULL);
        return;
    }
    const i2c_device_config_t device_configuration = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t device = NULL;
    error = i2c_master_bus_add_device(bus, &device_configuration, &device);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "I2C device allocation failed: %s", esp_err_to_name(error));
        (void)i2c_del_master_bus(bus);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Controller profile: %s, address=0x%02X", OLED_USE_SH1106 ? "SH1106" : "SSD1306",
             OLED_I2C_ADDRESS);
    ESP_LOGI(TAG, "UPDATE: full-page transfers, event-driven UI");
    vTaskDelay(pdMS_TO_TICKS(500));
    /* Static storage keeps these large buffers off the task stack. */
    static uint8_t frame[UI_FRAME_BYTES], last_frame[UI_FRAME_BYTES];
    bool initialized = false;
    bool have_frame = false;
    unsigned consecutive_failures = 0;
    int64_t shown_alarm_ms = -1;
    ui_state_t state;
    alarm_panel_init(&state);
    for (;;) {
        if (!initialized) {
            error = i2c_master_probe(bus, OLED_I2C_ADDRESS, 100);
            if (error == ESP_OK) {
                error = initialize_screen(device);
            }
            if (error != ESP_OK) {
                ESP_LOGW(TAG, "Display unavailable: %s; retrying", esp_err_to_name(error));
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            initialized = true;
            have_frame = false;
            ESP_LOGI(TAG, "OLED READY: screen writes accepted; verify visible text");
        }
        /* Changed alarm snapshots wake this task immediately. The timeout
         * refreshes the exit countdown; I2C cannot block the control task. */
        (void)xQueueReceive(state_mailbox, &state, pdMS_TO_TICKS(have_frame ? 200 : 20));
        ui_frame_render(frame, &state, esp_timer_get_time() / 1000);
        uint8_t dirty = ui_dirty_pages(frame, last_frame, !have_frame);
        int64_t write_ms = 0;
        if (dirty != 0) {
            int64_t write_started = esp_timer_get_time();
            error = flush_frame(device, frame, dirty);
            write_ms = (esp_timer_get_time() - write_started) / 1000;
            if (error != ESP_OK) {
                ++consecutive_failures;
                ESP_LOGW(TAG, "Screen update failed: %s; retry %u", esp_err_to_name(error),
                         consecutive_failures);
                have_frame = false; /* A partial write cannot be trusted. */
                if (consecutive_failures >= 3) {
                    initialized = false;
                    consecutive_failures = 0;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            memcpy(last_frame, frame, sizeof(frame));
            have_frame = true;
            consecutive_failures = 0;
        }
        if (state.mode == PANEL_ALARM && state.alarm_at_ms != shown_alarm_ms) {
            shown_alarm_ms = state.alarm_at_ms;
            ESP_LOGI(TAG,
                     "OLED ALARM WRITE OK: count=%" PRIu32 " %s-to-write-done=%" PRId64
                     "ms I2C=%" PRId64 "ms",
                     state.motion_count, state.alarm_from_report ? "RX" : "countdown",
                     esp_timer_get_time() / 1000 - state.alarm_at_ms, write_ms);
        }
    }
}

esp_err_t panel_display_start(void)
{
    state_mailbox = xQueueCreate(1, sizeof(ui_state_t));
    if (state_mailbox == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ui_state_t initial;
    alarm_panel_init(&initial);
    (void)xQueueOverwrite(state_mailbox, &initial);
    published_state = initial;
    have_published = true;
    if (xTaskCreate(display_task, "oled_ui", 4096, NULL, 1, NULL) != pdPASS) {
        vQueueDelete(state_mailbox);
        state_mailbox = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void panel_display_publish(const ui_state_t *state)
{
    if (state_mailbox == NULL) {
        return;
    }
    bool changed = !have_published || state->mode != published_state.mode ||
                   state->link != published_state.link || state->fault != published_state.fault ||
                   state->have_detector != published_state.have_detector ||
                   state->sensor_ready != published_state.sensor_ready ||
                   state->motion_high != published_state.motion_high ||
                   state->warmup_s != published_state.warmup_s ||
                   state->detector_session != published_state.detector_session ||
                   state->motion_count != published_state.motion_count ||
                   state->alarm_count != published_state.alarm_count ||
                   state->arm_deadline_ms != published_state.arm_deadline_ms ||
                   state->alarm_at_ms != published_state.alarm_at_ms ||
                   state->alarm_from_report != published_state.alarm_from_report;
    if (changed) {
        published_state = *state;
        have_published = true;
        (void)xQueueOverwrite(state_mailbox, state);
    }
}
