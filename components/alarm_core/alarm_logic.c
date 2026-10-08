#include "alarm_logic.h"
#include <string.h>

void alarm_panel_init(alarm_panel_t *p)
{
    memset(p, 0, sizeof(*p));
    p->mode = PANEL_DISARMED;
    p->link = LINK_WAITING;
}
static void latch(alarm_panel_t *p, int64_t now_ms, bool from_report)
{
    if (p->mode == PANEL_ALARM)
        return;
    p->mode = PANEL_ALARM;
    p->alarm_at_ms = now_ms;
    p->alarm_from_report = from_report;
    ++p->alarm_count;
}
void alarm_panel_set_link(alarm_panel_t *p, bool healthy)
{
    if (healthy)
        p->link = LINK_CONNECTED;
    else if (p->link != LINK_WAITING)
        p->link = LINK_LOST;
    if (!healthy && p->mode == PANEL_ARMING)
        p->mode = PANEL_DISARMED;
}
void alarm_panel_report(alarm_panel_t *p, const alarm_message_t *m, int64_t received_ms)
{
    bool new_session = !p->have_detector || m->session != p->detector_session;
    bool new_motion = !new_session && alarm_u32_newer(m->motion_count, p->motion_count);
    if (new_session && p->have_detector && (p->mode == PANEL_ARMED || p->mode == PANEL_ARMING)) {
        p->fault = FAULT_RESTART;
        if (p->mode == PANEL_ARMING)
            p->mode = PANEL_DISARMED;
    }
    if (!new_session && m->motion_count != p->motion_count && !new_motion &&
        (p->mode == PANEL_ARMED || p->mode == PANEL_ARMING)) {
        p->fault = FAULT_COUNTER;
        if (p->mode == PANEL_ARMING)
            p->mode = PANEL_DISARMED;
    }
    p->have_detector = true;
    p->detector_session = m->session;
    p->sensor_ready = (m->flags & ALARM_READY) != 0;
    p->motion_high = (m->flags & ALARM_HIGH) != 0;
    p->warmup_s = m->warmup_s;
    p->motion_count = m->motion_count;
    if (!p->sensor_ready && p->mode == PANEL_ARMING)
        p->mode = PANEL_DISARMED;
    if (p->mode == PANEL_ARMED && p->fault == FAULT_NONE && p->sensor_ready &&
        (new_motion || p->motion_high))
        latch(p, received_ms, true);
    /* CLEAR, duplicate reports, timeouts, and peer restarts cannot unlatch ALARM. */
}
bool alarm_panel_arm(alarm_panel_t *p, int64_t now_ms)
{
    if (p->mode != PANEL_DISARMED || p->link != LINK_CONNECTED || !p->have_detector ||
        !p->sensor_ready || p->fault != FAULT_NONE)
        return false;
    p->mode = PANEL_ARMING;
    p->arm_deadline_ms = now_ms + ALARM_EXIT_MS;
    return true;
}
void alarm_panel_disarm(alarm_panel_t *p)
{
    p->mode = PANEL_DISARMED;
    p->fault = FAULT_NONE;
    p->arm_deadline_ms = 0;
}
void alarm_panel_tick(alarm_panel_t *p, int64_t now_ms)
{
    if (p->mode != PANEL_ARMING || now_ms < p->arm_deadline_ms)
        return;
    if (p->link != LINK_CONNECTED || !p->sensor_ready || p->fault != FAULT_NONE) {
        p->mode = PANEL_DISARMED;
        return;
    }
    p->mode = PANEL_ARMED;
    if (p->motion_high)
        latch(p, now_ms, false);
}
unsigned alarm_panel_countdown_s(const alarm_panel_t *p, int64_t now_ms)
{
    if (p->mode != PANEL_ARMING || now_ms >= p->arm_deadline_ms)
        return 0;
    return (unsigned)((p->arm_deadline_ms - now_ms + 999) / 1000);
}
const char *alarm_panel_mode_name(panel_mode_t mode)
{
    switch (mode) {
    case PANEL_DISARMED:
        return "DISARMED";
    case PANEL_ARMING:
        return "ARMING";
    case PANEL_ARMED:
        return "ARMED";
    case PANEL_ALARM:
        return "ALARM";
    default:
        return "UNKNOWN";
    }
}
void alarm_debounce_init(alarm_debounce_t *d, bool level, int64_t now_ms)
{
    d->candidate = level;
    d->stable = level;
    d->since_ms = now_ms;
}
bool alarm_debounce_sample(alarm_debounce_t *d, bool level, int64_t now_ms, unsigned stable_ms)
{
    if (level != d->candidate) {
        d->candidate = level;
        d->since_ms = now_ms;
    }
    if (d->stable == d->candidate || now_ms - d->since_ms < stable_ms)
        return false;
    d->stable = d->candidate;
    return true;
}
void alarm_detector_init(alarm_detector_t *d, bool high, int64_t now_ms, unsigned warmup_ms)
{
    memset(d, 0, sizeof(*d));
    alarm_debounce_init(&d->input, high, now_ms);
    d->ready_at_ms = now_ms + warmup_ms;
}
bool alarm_detector_sample(alarm_detector_t *d, bool high, int64_t now_ms, unsigned stable_ms)
{
    bool edge = alarm_debounce_sample(&d->input, high, now_ms, stable_ms);
    if (!d->ready && now_ms < d->ready_at_ms)
        return false;
    bool changed = !d->ready || edge;
    if (!changed)
        return false;
    bool rising = d->input.stable && (!d->ready || !d->high);
    d->ready = true;
    d->high = d->input.stable;
    if (rising) {
        ++d->motion_count;
        d->last_motion_ms = now_ms;
        d->have_motion = true;
    }
    return true;
}
alarm_message_t alarm_detector_snapshot(const alarm_detector_t *d, int64_t now_ms)
{
    alarm_message_t m = {.type = ALARM_STATE,
                         .role = ALARM_DETECTOR,
                         .motion_count = d->motion_count,
                         .event_age_ms = UINT32_MAX};
    if (d->ready)
        m.flags = ALARM_READY | (d->high ? ALARM_HIGH : 0);
    else {
        int64_t remaining = d->ready_at_ms > now_ms ? d->ready_at_ms - now_ms : 0;
        uint64_t seconds = (uint64_t)(remaining + 999) / 1000;
        m.warmup_s = seconds > UINT16_MAX ? UINT16_MAX : (uint16_t)seconds;
    }
    if (d->have_motion) {
        uint64_t age = now_ms > d->last_motion_ms ? (uint64_t)(now_ms - d->last_motion_ms) : 0;
        m.event_age_ms = age >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)age;
    }
    return m;
}
void alarm_tx_init(alarm_tx_t *tx, alarm_role_t role, uint32_t session)
{
    memset(tx, 0, sizeof(*tx));
    tx->role = role;
    tx->session = session;
}
void alarm_tx_replace(alarm_tx_t *tx, const alarm_message_t *snapshot, int64_t now_ms)
{
    if (tx->active)
        ++tx->superseded;
    tx->pending = *snapshot;
    tx->pending.type = ALARM_STATE;
    tx->pending.role = tx->role;
    tx->pending.session = tx->session;
    tx->pending.sequence = ++tx->next_sequence;
    tx->active = true;
    tx->attempts = 0;
    tx->failure_reported = false;
    tx->due_ms = now_ms;
}
bool alarm_tx_due(const alarm_tx_t *tx, int64_t now_ms)
{
    return tx->active && tx->attempts < ALARM_TX_ATTEMPTS && now_ms >= tx->due_ms;
}
void alarm_tx_sent(alarm_tx_t *tx, int64_t now_ms)
{
    if (tx->attempts > 0)
        ++tx->retries;
    ++tx->attempts;
    ++tx->sent;
    tx->last_sent_ms = now_ms;
    tx->due_ms = now_ms + ALARM_ACK_TIMEOUT_MS;
}
bool alarm_tx_expired(alarm_tx_t *tx, int64_t now_ms)
{
    if (!tx->active || tx->failure_reported || tx->attempts < ALARM_TX_ATTEMPTS ||
        now_ms < tx->due_ms)
        return false;
    tx->failure_reported = true;
    ++tx->unconfirmed;
    return true;
}
bool alarm_tx_ack(alarm_tx_t *tx, const alarm_message_t *ack, int64_t now_ms)
{
    if (!tx->active || tx->attempts == 0 || ack->type != ALARM_ACK ||
        ack->role != (tx->role == ALARM_DETECTOR ? ALARM_PANEL : ALARM_DETECTOR) ||
        ack->session != tx->session || ack->sequence != tx->pending.sequence)
        return false;
    tx->active = false;
    tx->have_ack = true;
    tx->last_ack_ms = now_ms;
    ++tx->acknowledged;
    return true;
}
