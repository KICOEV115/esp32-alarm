#ifndef ALARM_LOGIC_H
#define ALARM_LOGIC_H
#include "alarm_protocol.h"

enum {
    ALARM_EXIT_MS = 10000,
    ALARM_HEARTBEAT_MS = 500,
    ALARM_LINK_TIMEOUT_MS = 2500,
    ALARM_ACK_TIMEOUT_MS = 120,
    ALARM_TX_ATTEMPTS = 3
};
typedef enum { PANEL_DISARMED, PANEL_ARMING, PANEL_ARMED, PANEL_ALARM } panel_mode_t;
typedef enum { LINK_WAITING, LINK_CONNECTED, LINK_LOST } panel_link_t;
typedef enum { FAULT_NONE, FAULT_RESTART, FAULT_COUNTER } panel_fault_t;
typedef struct {
    panel_mode_t mode;
    panel_link_t link;
    panel_fault_t fault;
    bool have_detector, sensor_ready, motion_high;
    uint16_t warmup_s;
    uint32_t detector_session, motion_count, alarm_count;
    int64_t arm_deadline_ms, alarm_at_ms;
    bool alarm_from_report;
} alarm_panel_t;

void alarm_panel_init(alarm_panel_t *panel);
void alarm_panel_set_link(alarm_panel_t *panel, bool healthy);
/* Call only for sequence-filtered detector STATE messages. */
void alarm_panel_report(alarm_panel_t *panel, const alarm_message_t *message, int64_t received_ms);
bool alarm_panel_arm(alarm_panel_t *panel, int64_t now_ms);
void alarm_panel_disarm(alarm_panel_t *panel);
void alarm_panel_tick(alarm_panel_t *panel, int64_t now_ms);
unsigned alarm_panel_countdown_s(const alarm_panel_t *panel, int64_t now_ms);
const char *alarm_panel_mode_name(panel_mode_t mode);

typedef struct {
    bool candidate, stable;
    int64_t since_ms;
} alarm_debounce_t;
void alarm_debounce_init(alarm_debounce_t *input, bool level, int64_t now_ms);
bool alarm_debounce_sample(alarm_debounce_t *input, bool level, int64_t now_ms, unsigned stable_ms);

typedef struct {
    alarm_debounce_t input;
    int64_t ready_at_ms, last_motion_ms;
    bool ready, high, have_motion;
    uint32_t motion_count;
} alarm_detector_t;
void alarm_detector_init(alarm_detector_t *detector, bool raw_high, int64_t now_ms,
                         unsigned warmup_ms);
/* Returns true when readiness or the stable input changes. */
bool alarm_detector_sample(alarm_detector_t *detector, bool raw_high, int64_t now_ms,
                           unsigned stable_ms);
alarm_message_t alarm_detector_snapshot(const alarm_detector_t *detector, int64_t now_ms);

typedef struct {
    alarm_role_t role;
    uint32_t session, next_sequence;
    alarm_message_t pending;
    bool active, failure_reported, have_ack;
    unsigned attempts;
    int64_t due_ms, last_sent_ms, last_ack_ms;
    uint32_t sent, retries, unconfirmed, acknowledged, superseded;
} alarm_tx_t;
void alarm_tx_init(alarm_tx_t *tx, alarm_role_t role, uint32_t session);
/* A new snapshot immediately supersedes an unacknowledged heartbeat.
 * Detector snapshots retain ALL motion occurrences in their counter. */
void alarm_tx_replace(alarm_tx_t *tx, const alarm_message_t *snapshot, int64_t now_ms);
bool alarm_tx_due(const alarm_tx_t *tx, int64_t now_ms);
void alarm_tx_sent(alarm_tx_t *tx, int64_t now_ms);
bool alarm_tx_expired(alarm_tx_t *tx, int64_t now_ms);
bool alarm_tx_ack(alarm_tx_t *tx, const alarm_message_t *ack, int64_t now_ms);
#endif
