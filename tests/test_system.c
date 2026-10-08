#include <assert.h>
#include <stdio.h>
#include "alarm_logic.h"

static alarm_message_t report(uint32_t count, bool high)
{
    alarm_message_t m = {.type = ALARM_STATE,
                         .role = ALARM_DETECTOR,
                         .session = 100,
                         .sequence = count + 1,
                         .motion_count = count,
                         .flags = ALARM_READY | (high ? ALARM_HIGH : 0),
                         .event_age_ms = 0};
    return m;
}
static void ready_panel(alarm_panel_t *p, uint32_t count)
{
    alarm_panel_init(p);
    alarm_message_t m = report(count, false);
    alarm_panel_report(p, &m, 0);
    alarm_panel_set_link(p, true);
}
static void arm(alarm_panel_t *p)
{
    assert(alarm_panel_arm(p, 100));
    assert(alarm_panel_countdown_s(p, 100) == 10);
    alarm_panel_tick(p, 10099);
    assert(p->mode == PANEL_ARMING);
    alarm_panel_tick(p, 10100);
    assert(p->mode == PANEL_ARMED);
}
static bool deliver(alarm_rx_window_t *w, alarm_panel_t *p, const alarm_message_t *m, int64_t now)
{
    uint8_t bytes[ALARM_PACKET_SIZE];
    alarm_message_t decoded;
    assert(alarm_encode(m, bytes, sizeof(bytes)));
    assert(alarm_decode(bytes, sizeof(bytes), &decoded));
    alarm_rx_result_t result = alarm_rx_accept(w, &decoded);
    if (result == ALARM_RX_FRESH || result == ALARM_RX_NEW_SESSION) {
        alarm_panel_report(p, &decoded, now);
        return true;
    }
    return false;
}
int main(void)
{
    alarm_panel_t p;
    alarm_panel_init(&p);
    assert(!alarm_panel_arm(&p, 0));
    alarm_message_t m = {.type = ALARM_STATE,
                         .role = ALARM_DETECTOR,
                         .session = 100,
                         .event_age_ms = UINT32_MAX,
                         .warmup_s = 60};
    alarm_panel_report(&p, &m, 0);
    alarm_panel_set_link(&p, true);
    assert(!alarm_panel_arm(&p, 0)); /* Sensor warm-up blocks arming. */
    ready_panel(&p, 0);
    assert(alarm_panel_arm(&p, 100));
    alarm_panel_set_link(&p, false);
    assert(p.mode == PANEL_DISARMED && p.link == LINK_LOST);
    alarm_panel_tick(&p, 20000);
    assert(p.mode == PANEL_DISARMED);
    ready_panel(&p, 0);
    arm(&p);
    m = report(1, true);
    alarm_panel_report(&p, &m, 11000);
    assert(p.mode == PANEL_ALARM && p.alarm_count == 1 && p.alarm_at_ms == 11000);
    m = report(1, false);
    alarm_panel_report(&p, &m, 11020);
    assert(p.mode == PANEL_ALARM);
    alarm_panel_set_link(&p, false);
    alarm_panel_tick(&p, 90000);
    assert(p.mode == PANEL_ALARM && p.alarm_count == 1);
    alarm_panel_disarm(&p);
    assert(p.mode == PANEL_DISARMED && p.link == LINK_LOST);
    alarm_panel_set_link(&p, true);
    arm(&p);
    alarm_panel_report(&p, &m, 11000);
    assert(p.mode == PANEL_ARMED); /* Already-seen count. */
    m = report(2, false);
    alarm_panel_report(&p, &m, 11020);
    assert(p.mode == PANEL_ALARM); /* HIGH packet lost; later CLEAR retains new count. */
    puts("PASS latch: CLEAR/timeouts cannot silence ALARM; counter recovers a lost HIGH packet");

    ready_panel(&p, 0);
    assert(alarm_panel_arm(&p, 100));
    m = report(1, true);
    alarm_panel_report(&p, &m, 500);
    m = report(1, false);
    alarm_panel_report(&p, &m, 600);
    alarm_panel_tick(&p, 10100);
    assert(p.mode == PANEL_ARMED); /* Exit motion finished. */
    ready_panel(&p, 0);
    assert(alarm_panel_arm(&p, 100));
    m = report(1, true);
    alarm_panel_report(&p, &m, 500);
    alarm_panel_tick(&p, 10100);
    assert(p.mode == PANEL_ALARM && !p.alarm_from_report);
    ready_panel(&p, 0);
    arm(&p);
    m = report(0, false);
    m.session = 200;
    alarm_panel_report(&p, &m, 11000);
    assert(p.mode == PANEL_ARMED && p.fault == FAULT_RESTART);
    assert(!alarm_panel_arm(&p, 11001));
    alarm_panel_disarm(&p);
    assert(p.fault == FAULT_NONE && alarm_panel_arm(&p, 12000));
    ready_panel(&p, 5);
    arm(&p);
    m = report(4, false);
    alarm_panel_report(&p, &m, 11000);
    assert(p.fault == FAULT_COUNTER);
    ready_panel(&p, UINT32_MAX);
    arm(&p);
    m = report(0, false);
    alarm_panel_report(&p, &m, 11000);
    assert(p.mode == PANEL_ALARM && p.fault == FAULT_NONE);
    puts("PASS control: countdown, loss cancellation, motion at arm boundary, reboot fault, "
         "counter regression/wrap");

    alarm_detector_t d;
    alarm_detector_init(&d, false, 0, 60000);
    assert(!alarm_detector_sample(&d, true, 10, 20));
    assert(!alarm_detector_sample(&d, true, 30, 20));
    assert(d.motion_count == 0 && !d.ready);
    assert(alarm_detector_sample(&d, true, 60000, 20));
    assert(d.motion_count == 1 && d.high);
    alarm_detector_init(&d, false, 0, 0);
    assert(alarm_detector_sample(&d, false, 0, 20));
    assert(!alarm_detector_sample(&d, true, 10, 20));
    assert(!alarm_detector_sample(&d, true, 29, 20));
    assert(alarm_detector_sample(&d, true, 30, 20));
    assert(!alarm_detector_sample(&d, false, 40, 20));
    assert(alarm_detector_sample(&d, false, 60, 20));
    assert(!alarm_detector_sample(&d, true, 80, 20));
    assert(alarm_detector_sample(&d, true, 100, 20));
    assert(d.motion_count == 2);
    m = alarm_detector_snapshot(&d, 120);
    assert(m.motion_count == 2 && m.event_age_ms == 20);
    puts("PASS input: warm-up, 20ms stability filter, second rising edge accepted without software "
         "cooldown");

    alarm_tx_t tx;
    alarm_tx_init(&tx, ALARM_DETECTOR, 100);
    alarm_message_t clear = report(0, false);
    alarm_tx_replace(&tx, &clear, 0);
    alarm_tx_sent(&tx, 0);
    uint32_t heartbeat_seq = tx.pending.sequence;
    m = report(1, true);
    alarm_tx_replace(&tx, &m, 20);
    assert(alarm_tx_due(&tx, 20) && tx.pending.sequence != heartbeat_seq);
    assert(tx.pending.motion_count == 1); /* New motion preempts waiting heartbeat. */
    alarm_tx_sent(&tx, 20);
    assert(!alarm_tx_due(&tx, 139));
    assert(alarm_tx_due(&tx, 140));
    alarm_tx_sent(&tx, 140);
    assert(alarm_tx_due(&tx, 260));
    alarm_tx_sent(&tx, 260);
    assert(!alarm_tx_expired(&tx, 379));
    assert(alarm_tx_expired(&tx, 380));
    assert(!alarm_tx_expired(&tx, 381) && tx.unconfirmed == 1 && tx.retries == 2);
    alarm_message_t ack = alarm_ack_for(&tx.pending, ALARM_PANEL);
    ack.sequence = heartbeat_seq;
    assert(!alarm_tx_ack(&tx, &ack, 400));
    ack.sequence = tx.pending.sequence;
    ack.session++;
    assert(!alarm_tx_ack(&tx, &ack, 400));
    ack.session = tx.session;
    ack.role = (alarm_role_t)0;
    assert(!alarm_tx_ack(&tx, &ack, 400));
    ack.role = ALARM_PANEL;
    assert(alarm_tx_ack(&tx, &ack, 400));
    assert(!alarm_tx_ack(&tx, &ack, 401));
    puts("PASS sender: immediate event preemption, retry deadlines/budget, late/wrong/duplicate "
         "ACK rejection");

    /* End-to-end portable replay: encode -> loss -> retry -> dedup -> latch. */
    ready_panel(&p, 0);
    arm(&p);
    alarm_rx_window_t window = {0};
    clear.sequence = 1;
    assert(deliver(&window, &p, &clear, 10500));
    alarm_tx_init(&tx, ALARM_DETECTOR, 100);
    tx.next_sequence = 1;
    m = report(1, true);
    alarm_tx_replace(&tx, &m, 11000);
    alarm_tx_sent(&tx, 11000);
    assert(deliver(&window, &p, &tx.pending, 11005));
    assert(p.alarm_count == 1);
    /* First application ACK deliberately lost. */
    assert(alarm_tx_due(&tx, 11120));
    alarm_tx_sent(&tx, 11120);
    assert(!deliver(&window, &p, &tx.pending, 11125));
    assert(p.alarm_count == 1);
    ack = alarm_ack_for(&tx.pending, ALARM_PANEL);
    assert(alarm_tx_ack(&tx, &ack, 11130));
    alarm_message_t old_motion = tx.pending;
    m = report(1, false);
    alarm_tx_replace(&tx, &m, 11140);
    assert(deliver(&window, &p, &tx.pending, 11145));
    assert(p.mode == PANEL_ALARM);
    alarm_panel_disarm(&p);
    assert(alarm_panel_arm(&p, 12000));
    alarm_panel_tick(&p, 22000);
    assert(p.mode == PANEL_ARMED);
    assert(!deliver(&window, &p, &old_motion, 22005));
    assert(p.mode == PANEL_ARMED);
    /* HIGH AND CLEAR snapshots lost; the next periodic snapshot recovers count 2. */
    m = report(2, false);
    alarm_tx_replace(&tx, &m, 22500);
    alarm_tx_sent(&tx, 22500);
    assert(deliver(&window, &p, &tx.pending, 22505));
    assert(p.mode == PANEL_ALARM);
    assert(p.alarm_count == 2 && p.motion_count == 2);
    puts("PASS replay: dropped ACK causes retry without double alarm; old replay after rearm "
         "ignored; later snapshot recovers lost motion and clear");
    return 0;
}
