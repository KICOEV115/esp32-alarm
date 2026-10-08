#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "alarm_app.h"
#include "alarm_logic.h"
#include "board_config.h"
#include "panel_display.h"
#include "panel_outputs.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define MAC_FORMAT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_VALUES(m) (m)[0], (m)[1], (m)[2], (m)[3], (m)[4], (m)[5]
enum { RADIO_CHANNEL = 1, RX_QUEUE_LENGTH = 24, ACK_QUEUE_LENGTH = 16 };
static const uint8_t BROADCAST[6] = {255, 255, 255, 255, 255, 255};
static const char *TAG;
static QueueHandle_t rx_queue, ack_queue;
static SemaphoreHandle_t tx_available;
static portMUX_TYPE callback_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t callback_rx_dropped, callback_mac_failures;

typedef struct {
    uint8_t source[6], data[ALARM_PACKET_SIZE];
    int64_t received_ms;
} rx_event_t;
typedef struct {
    alarm_role_t role;
    uint8_t own_mac[6], peer_mac[6];
    bool peer_known, have_state;
    int64_t last_state_ms;
    alarm_rx_window_t window;
    alarm_tx_t tx;
    uint32_t duplicates, older, malformed, ack_dropped, state_dropped, state_seen;
    uint32_t local_send_failures, ack_queue_dropped;
    bool test_have_ack_drop;
    uint32_t test_drop_session, test_drop_sequence;
} radio_state_t;
static int64_t milliseconds(void)
{
    return esp_timer_get_time() / 1000;
}

/* Both callbacks run in the Wi-Fi task. Only copy/enqueue or release the
 * radio gate here; no state machine, logging, GPIO, or I2C operations. */
static void on_receive(const esp_now_recv_info_t *info, const uint8_t *bytes, int length)
{
    if (info == NULL || info->src_addr == NULL || bytes == NULL || length != ALARM_PACKET_SIZE)
        return;
    rx_event_t event = {.received_ms = milliseconds()};
    memcpy(event.source, info->src_addr, 6);
    memcpy(event.data, bytes, ALARM_PACKET_SIZE);
    if (xQueueSend(rx_queue, &event, 0) != pdTRUE) {
        portENTER_CRITICAL(&callback_lock);
        ++callback_rx_dropped;
        portEXIT_CRITICAL(&callback_lock);
    }
}
static void on_send(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    if (status != ESP_NOW_SEND_SUCCESS) {
        portENTER_CRITICAL(&callback_lock);
        ++callback_mac_failures;
        portEXIT_CRITICAL(&callback_lock);
    }
    (void)xSemaphoreGive(tx_available);
}
static esp_err_t send_message(const uint8_t *destination, const alarm_message_t *m)
{
    uint8_t data[ALARM_PACKET_SIZE];
    if (!alarm_encode(m, data, sizeof(data)))
        return ESP_ERR_INVALID_ARG;
    /* Zero wait: the input/control loop must never wait for a send callback. */
    if (xSemaphoreTake(tx_available, 0) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t error = esp_now_send(destination, data, sizeof(data));
    if (error != ESP_OK)
        (void)xSemaphoreGive(tx_available);
    return error;
}
static esp_err_t add_peer(const uint8_t *address)
{
    if (esp_now_is_peer_exist(address))
        return ESP_OK;
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, address, 6);
    peer.channel = RADIO_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer);
}
static void start_radio(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_receive));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_send));
    ESP_ERROR_CHECK(add_peer(BROADCAST));
}
static void queue_ack(radio_state_t *radio, const alarm_message_t *m)
{
    if (radio->role == ALARM_PANEL && TEST_DROP_FIRST_ACK &&
        (!radio->test_have_ack_drop || radio->test_drop_session != m->session ||
         radio->test_drop_sequence != m->sequence)) {
        radio->test_have_ack_drop = true;
        radio->test_drop_session = m->session;
        radio->test_drop_sequence = m->sequence;
        ++radio->ack_dropped;
        ESP_LOGW(TAG, "TEST ACK DROP seq=%" PRIu32, m->sequence);
        return;
    }
    alarm_message_t ack = alarm_ack_for(m, radio->role);
    if (xQueueSend(ack_queue, &ack, 0) != pdTRUE)
        ++radio->ack_queue_dropped;
}
static bool receive_message(radio_state_t *radio, alarm_panel_t *panel, const rx_event_t *event)
{
    alarm_message_t m;
    if (!alarm_decode(event->data, sizeof(event->data), &m)) {
        ++radio->malformed;
        return false;
    }
    if (m.role == radio->role || memcmp(event->source, radio->own_mac, 6) == 0)
        return false;
    bool discovered = false;
    if (!radio->peer_known) {
        if (m.type != ALARM_STATE)
            return false;
        esp_err_t error = add_peer(event->source);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "Cannot add peer: %s", esp_err_to_name(error));
            return false;
        }
        memcpy(radio->peer_mac, event->source, 6);
        radio->peer_known = true;
        discovered = true;
        ESP_LOGI(TAG, "PEER discovered " MAC_FORMAT, MAC_VALUES(radio->peer_mac));
    }
    if (memcmp(event->source, radio->peer_mac, 6) != 0)
        return false;
    if (m.type == ALARM_ACK) {
        (void)alarm_tx_ack(&radio->tx, &m, event->received_ms);
        return false;
    }
    ++radio->state_seen;
#if TEST_DROP_EVERY_N_STATE > 0
    if (radio->role == ALARM_PANEL && radio->state_seen % TEST_DROP_EVERY_N_STATE == 0) {
        ++radio->state_dropped;
        ESP_LOGW(TAG, "TEST STATE DROP seq=%" PRIu32, m.sequence);
        return discovered;
    }
#endif
    alarm_rx_result_t result = alarm_rx_accept(&radio->window, &m);
    if (result == ALARM_RX_FRESH || result == ALARM_RX_NEW_SESSION) {
        radio->have_state = true;
        radio->last_state_ms = event->received_ms;
        if (result == ALARM_RX_NEW_SESSION)
            radio->tx.have_ack = false;
        if (radio->role == ALARM_PANEL) {
            bool changed = !panel->have_detector || panel->detector_session != m.session ||
                           panel->motion_count != m.motion_count ||
                           panel->sensor_ready != ((m.flags & ALARM_READY) != 0) ||
                           panel->motion_high != ((m.flags & ALARM_HIGH) != 0);
            alarm_panel_report(panel, &m, event->received_ms);
            if (changed) {
                ESP_LOGI(TAG, "RX STATE seq=%" PRIu32 " count=%" PRIu32 " GPIO27=%d ready=%d",
                         m.sequence, m.motion_count, (m.flags & ALARM_HIGH) != 0,
                         (m.flags & ALARM_READY) != 0);
                if (m.event_age_ms != UINT32_MAX)
                    ESP_LOGI(TAG, "Snapshot motion age=%" PRIu32 "ms (detector clock)",
                             m.event_age_ms);
            }
        }
    } else if (result == ALARM_RX_DUPLICATE) {
        ++radio->duplicates;
        ESP_LOGI(TAG, "DUPLICATE ignored seq=%" PRIu32, m.sequence);
    } else
        ++radio->older;
    /* Acknowledge after decoding/filtering/application processing, including
     * duplicate packets. This lets an ACK-lost sender stop its retry. */
    queue_ack(radio, &m);
    return discovered || result == ALARM_RX_NEW_SESSION;
}
static void start_inputs(alarm_role_t role)
{
    const gpio_config_t input = {
        .pin_bit_mask = role == ALARM_DETECTOR ? UINT64_C(1) << PIR_GPIO
                                               : (UINT64_C(1) << ARM_BUTTON_GPIO) |
                                                     (UINT64_C(1) << DISARM_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en =
            role == ALARM_PANEL || DETECTOR_BUTTON_TEST ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = role == ALARM_DETECTOR && !DETECTOR_BUTTON_TEST ? GPIO_PULLDOWN_ENABLE
                                                                        : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input));
}
static bool detector_level(void)
{
    bool level = gpio_get_level(PIR_GPIO) != 0;
    return DETECTOR_BUTTON_TEST ? !level : level;
}
void alarm_app_run(alarm_role_t role)
{
    TAG = role == ALARM_DETECTOR ? "detector" : "panel";
    vTaskPrioritySet(NULL, 4);
    rx_queue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(rx_event_t));
    ack_queue = xQueueCreate(ACK_QUEUE_LENGTH, sizeof(alarm_message_t));
    tx_available = xSemaphoreCreateBinary();
    if (rx_queue == NULL || ack_queue == NULL || tx_available == NULL) {
        ESP_LOGE(TAG, "Cannot allocate communication resources");
        return;
    }
    (void)xSemaphoreGive(tx_available);
    start_inputs(role);
    if (role == ALARM_PANEL) {
        ESP_ERROR_CHECK(panel_outputs_start());
        esp_err_t error = panel_display_start();
        if (error != ESP_OK)
            ESP_LOGW(TAG, "OLED task unavailable: %s", esp_err_to_name(error));
    }
    start_radio();
    radio_state_t radio = {.role = role};
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, radio.own_mac));
    uint32_t session = esp_random();
    alarm_tx_init(&radio.tx, role, session);
    alarm_panel_t panel;
    alarm_panel_init(&panel);
    int64_t now = milliseconds();
    alarm_detector_t detector;
    alarm_detector_init(&detector, role == ALARM_DETECTOR ? detector_level() : false, now,
                        DETECTOR_BUTTON_TEST ? 0 : PIR_WARMUP_MS);
    alarm_debounce_t arm_button, disarm_button;
    alarm_debounce_init(&arm_button, role == ALARM_PANEL && gpio_get_level(ARM_BUTTON_GPIO) == 0,
                        now);
    alarm_debounce_init(&disarm_button,
                        role == ALARM_PANEL && gpio_get_level(DISARM_BUTTON_GPIO) == 0, now);
    ESP_LOGI(TAG, "READY protocol=%d role=%s mac=" MAC_FORMAT " session=%08" PRIx32,
             ALARM_PROTOCOL_VERSION, role == ALARM_DETECTOR ? "DETECTOR" : "PANEL",
             MAC_VALUES(radio.own_mac), session);
    ESP_LOGI(TAG, "Input poll=%dms filter=%dms heartbeat=%dms ACK=%dms fault=%dms",
             CONTROL_PERIOD_MS, INPUT_STABLE_MS, ALARM_HEARTBEAT_MS, ALARM_ACK_TIMEOUT_MS,
             ALARM_LINK_TIMEOUT_MS);
    if (role == ALARM_DETECTOR)
        ESP_LOGI(TAG, "GPIO27 input=%s, warm-up=%dms",
                 DETECTOR_BUTTON_TEST ? "BUTTON TEST (active low)" : "PIR (active high)",
                 DETECTOR_BUTTON_TEST ? 0 : PIR_WARMUP_MS);
    else
        ESP_LOGI(TAG, "ARM=GPIO%d->GND DISARM=GPIO%d->GND; exit countdown=%dms", ARM_BUTTON_GPIO,
                 DISARM_BUTTON_GPIO, ALARM_EXIT_MS);
    bool health_was_up = false;
    panel_mode_t previous_mode = PANEL_DISARMED;
    panel_fault_t previous_fault = FAULT_NONE;
    unsigned previous_countdown = 0;
    int64_t next_heartbeat = now, next_stats = now + 5000, edge_ms = 0;
    uint32_t edge_sequence = 0;
    TickType_t wake_at = xTaskGetTickCount();
    const TickType_t period_ticks =
        pdMS_TO_TICKS(CONTROL_PERIOD_MS) > 0 ? pdMS_TO_TICKS(CONTROL_PERIOD_MS) : 1;
    for (;;) {
        now = milliseconds();
        bool force_snapshot = false, input_changed = false;
        if (role == ALARM_DETECTOR) {
            input_changed =
                alarm_detector_sample(&detector, detector_level(), now, INPUT_STABLE_MS);
            if (input_changed) {
                edge_ms = now;
                ESP_LOGI(TAG, "LOCAL %s count=%" PRIu32 " input-time=%" PRId64 "ms",
                         detector.high ? "MOTION" : "CLEAR", detector.motion_count, now);
            }
        }
        rx_event_t event;
        for (unsigned i = 0; i < 8 && xQueueReceive(rx_queue, &event, 0) == pdTRUE; ++i)
            force_snapshot = receive_message(&radio, &panel, &event) || force_snapshot;
        now = milliseconds();
        bool healthy = radio.peer_known && radio.have_state && radio.tx.have_ack &&
                       now - radio.last_state_ms < ALARM_LINK_TIMEOUT_MS &&
                       now - radio.tx.last_ack_ms < ALARM_LINK_TIMEOUT_MS;
        if (healthy != health_was_up) {
            ESP_LOGI(TAG, "LINK %s: peer STATE and application ACK %s", healthy ? "UP" : "DOWN",
                     healthy ? "fresh" : "timed out");
            health_was_up = healthy;
        }
        if (role == ALARM_PANEL) {
            alarm_panel_set_link(&panel, healthy);
            bool arm = alarm_debounce_sample(&arm_button, gpio_get_level(ARM_BUTTON_GPIO) == 0, now,
                                             BUTTON_STABLE_MS) &&
                       arm_button.stable;
            bool disarm =
                alarm_debounce_sample(&disarm_button, gpio_get_level(DISARM_BUTTON_GPIO) == 0, now,
                                      BUTTON_STABLE_MS) &&
                disarm_button.stable;
            if (disarm)
                alarm_panel_disarm(&panel); /* DISARM wins simultaneous presses. */
            else if (arm && !alarm_panel_arm(&panel, now))
                ESP_LOGW(TAG, "ARM rejected: need DISARMED, healthy link, PIR READY, no fault");
            alarm_panel_tick(&panel, now);
            panel_outputs_update(&panel); /* Output precedes OLED publication. */
            if (panel.mode != previous_mode) {
                ESP_LOGI(TAG, "MODE %s", alarm_panel_mode_name(panel.mode));
                if (panel.mode == PANEL_ALARM && panel.alarm_from_report)
                    ESP_LOGI(TAG, "TIMING count=%" PRIu32 " RX-to-output-call=%" PRId64 "ms",
                             panel.motion_count, milliseconds() - panel.alarm_at_ms);
                previous_mode = panel.mode;
            }
            if (panel.fault != previous_fault) {
                if (panel.fault != FAULT_NONE)
                    ESP_LOGW(TAG, "FAULT: %s; DISARM required",
                             panel.fault == FAULT_RESTART ? "detector rebooted"
                                                          : "counter moved backwards");
                previous_fault = panel.fault;
            }
            unsigned seconds = alarm_panel_countdown_s(&panel, now);
            if (seconds != previous_countdown) {
                if (seconds != 0)
                    ESP_LOGI(TAG, "EXIT countdown %us", seconds);
                previous_countdown = seconds;
            }
            panel_display_publish(&panel);
        }
        if (alarm_tx_expired(&radio.tx, now) && radio.peer_known)
            ESP_LOGW(TAG, "NOT CONFIRMED seq=%" PRIu32 "; next snapshot retains motion count",
                     radio.tx.pending.sequence);
        if (input_changed || force_snapshot || now >= next_heartbeat) {
            alarm_message_t snapshot = role == ALARM_DETECTOR
                                           ? alarm_detector_snapshot(&detector, now)
                                           : (alarm_message_t){.type = ALARM_STATE,
                                                               .role = ALARM_PANEL,
                                                               .event_age_ms = UINT32_MAX};
            alarm_tx_replace(&radio.tx, &snapshot, now);
            if (input_changed)
                edge_sequence = radio.tx.pending.sequence;
            next_heartbeat = now + ALARM_HEARTBEAT_MS;
        }
        /* ACKs have radio priority, but all sends are nonblocking. */
        alarm_message_t ack;
        if (radio.peer_known && xQueuePeek(ack_queue, &ack, 0) == pdTRUE) {
            esp_err_t error = send_message(radio.peer_mac, &ack);
            if (error == ESP_OK)
                (void)xQueueReceive(ack_queue, &ack, 0);
            else if (error != ESP_ERR_TIMEOUT)
                ++radio.local_send_failures;
        }
        if (alarm_tx_due(&radio.tx, now)) {
            const uint8_t *destination = radio.peer_known ? radio.peer_mac : BROADCAST;
            esp_err_t error = send_message(destination, &radio.tx.pending);
            if (error == ESP_OK) {
                if (radio.tx.attempts == 0 && role == ALARM_DETECTOR &&
                    radio.tx.pending.sequence == edge_sequence)
                    ESP_LOGI(
                        TAG,
                        "TIMING seq=%" PRIu32 " count=%" PRIu32 " stable-input-to-TX=%" PRId64 "ms",
                        radio.tx.pending.sequence, detector.motion_count, milliseconds() - edge_ms);
                if (radio.tx.attempts != 0)
                    ESP_LOGI(TAG, "RETRY seq=%" PRIu32 " attempt=%u", radio.tx.pending.sequence,
                             radio.tx.attempts + 1);
                alarm_tx_sent(&radio.tx, milliseconds());
            } else if (error != ESP_ERR_TIMEOUT) {
                ++radio.local_send_failures;
                radio.tx.due_ms = now + 10;
            }
        }
        if (now >= next_stats) {
            uint32_t rx_dropped, mac_failures;
            portENTER_CRITICAL(&callback_lock);
            rx_dropped = callback_rx_dropped;
            mac_failures = callback_mac_failures;
            portEXIT_CRITICAL(&callback_lock);
            ESP_LOGI(TAG,
                     "STATS tx=%" PRIu32 " retries=%" PRIu32 " unconfirmed=%" PRIu32
                     " acked=%" PRIu32 " duplicates=%" PRIu32 " older=%" PRIu32 " bad=%" PRIu32,
                     radio.tx.sent, radio.tx.retries, radio.tx.unconfirmed, radio.tx.acknowledged,
                     radio.duplicates, radio.older, radio.malformed);
            ESP_LOGI(TAG,
                     "STATS rx-overflow=%" PRIu32 " ack-overflow=%" PRIu32 " send-errors=%" PRIu32
                     " mac-failures=%" PRIu32 " test-ack-drops=%" PRIu32
                     " test-state-drops=%" PRIu32,
                     rx_dropped, radio.ack_queue_dropped, radio.local_send_failures, mac_failures,
                     radio.ack_dropped, radio.state_dropped);
            next_stats = now + 5000;
        }
        vTaskDelayUntil(&wake_at, period_ticks);
    }
}
