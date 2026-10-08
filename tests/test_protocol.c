#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "alarm_protocol.h"

static void seal(uint8_t *bytes)
{
    uint16_t crc = alarm_crc16(bytes, 24);
    bytes[24] = (uint8_t)(crc >> 8);
    bytes[25] = (uint8_t)crc;
}
static void same(const alarm_message_t *a, const alarm_message_t *b)
{
    assert(a->type == b->type && a->role == b->role && a->flags == b->flags);
    assert(a->session == b->session && a->sequence == b->sequence);
    assert(a->motion_count == b->motion_count && a->event_age_ms == b->event_age_ms);
    assert(a->warmup_s == b->warmup_s);
}
int main(void)
{
    assert(alarm_crc16((const uint8_t *)"123456789", 9) == 0x29b1);
    alarm_message_t m = {.type = ALARM_STATE,
                         .role = ALARM_DETECTOR,
                         .flags = ALARM_READY | ALARM_HIGH,
                         .session = UINT32_MAX,
                         .sequence = UINT32_MAX,
                         .motion_count = UINT32_MAX - 1,
                         .event_age_ms = 1234};
    uint8_t bytes[ALARM_PACKET_SIZE], damaged[ALARM_PACKET_SIZE];
    alarm_message_t out;
    assert(alarm_encode(&m, bytes, sizeof(bytes)));
    assert(alarm_decode(bytes, sizeof(bytes), &out));
    same(&m, &out);
    assert(bytes[2] == 3 && bytes[6] == 255 && bytes[17] == 254);
    for (size_t i = 0; i < sizeof(bytes); ++i)
        for (unsigned bit = 0; bit < 8; ++bit) {
            memcpy(damaged, bytes, sizeof(bytes));
            damaged[i] ^= (uint8_t)(1u << bit);
            memset(&out, 0xa5, sizeof(out));
            alarm_message_t before;
            memcpy(&before, &out, sizeof(out));
            assert(!alarm_decode(damaged, sizeof(damaged), &out));
            assert(memcmp(&before, &out, sizeof(out)) == 0);
        }
    for (unsigned field = 0; field < 6; ++field) {
        memcpy(damaged, bytes, sizeof(bytes));
        if (field == 0)
            damaged[2] = 2;
        if (field == 1)
            damaged[3] = 9;
        if (field == 2)
            damaged[4] = 0;
        if (field == 3)
            damaged[5] = 0x80;
        if (field == 4)
            damaged[5] = ALARM_HIGH;
        if (field == 5)
            damaged[23] = 1;
        seal(damaged);
        assert(!alarm_decode(damaged, sizeof(damaged), &out));
    }
    assert(!alarm_decode(bytes, ALARM_PACKET_SIZE - 1, &out));
    assert(!alarm_decode(NULL, ALARM_PACKET_SIZE, &out));
    assert(!alarm_encode(&m, bytes, ALARM_PACKET_SIZE - 1));
    alarm_message_t ack = alarm_ack_for(&m, ALARM_PANEL);
    assert(alarm_encode(&ack, bytes, sizeof(bytes)));
    assert(alarm_decode(bytes, sizeof(bytes), &out));
    same(&ack, &out);
    ack.motion_count = 1;
    assert(!alarm_encode(&ack, bytes, sizeof(bytes)));
    alarm_message_t warm = {
        .type = ALARM_STATE, .role = ALARM_DETECTOR, .event_age_ms = UINT32_MAX, .warmup_s = 60};
    assert(alarm_encode(&warm, bytes, sizeof(bytes)));
    assert(alarm_decode(bytes, sizeof(bytes), &out));
    same(&warm, &out);
    alarm_message_t panel = {.type = ALARM_STATE, .role = ALARM_PANEL, .event_age_ms = UINT32_MAX};
    assert(alarm_encode(&panel, bytes, sizeof(bytes)));
    panel.flags = ALARM_READY;
    assert(!alarm_encode(&panel, bytes, sizeof(bytes)));

    alarm_rx_window_t w = {0};
    assert(alarm_rx_accept(&w, &m) == ALARM_RX_NEW_SESSION);
    assert(alarm_rx_accept(&w, &m) == ALARM_RX_DUPLICATE);
    --m.sequence;
    assert(alarm_rx_accept(&w, &m) == ALARM_RX_OLD);
    m.sequence = 0;
    assert(alarm_rx_accept(&w, &m) == ALARM_RX_FRESH);
    m.session = 10;
    m.sequence = 1;
    assert(alarm_rx_accept(&w, &m) == ALARM_RX_NEW_SESSION);
    m.session = UINT32_MAX;
    assert(alarm_rx_accept(&w, &m) == ALARM_RX_OLD);
    assert(w.session == 10); /* Delayed packet must not revert to the retired boot. */
    assert(alarm_u32_newer(0, UINT32_MAX));
    assert(!alarm_u32_newer(UINT32_C(0x80000000), 0));
    puts("PASS protocol: CRC, all 208 single-bit corruptions, strict fields, version/length, "
         "sequence wrap, duplicate/old/retired session");
    return 0;
}
