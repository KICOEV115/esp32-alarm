#include "alarm_protocol.h"

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}
static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static bool valid(const alarm_message_t *m)
{
    if ((m->role != ALARM_DETECTOR && m->role != ALARM_PANEL) ||
        (m->type != ALARM_STATE && m->type != ALARM_ACK) ||
        (m->flags & ~(ALARM_READY | ALARM_HIGH)) != 0)
        return false;
    if (m->type == ALARM_ACK)
        return m->flags == 0 && m->motion_count == 0 && m->event_age_ms == 0 && m->warmup_s == 0;
    if (m->role == ALARM_PANEL)
        return m->flags == 0 && m->motion_count == 0 && m->event_age_ms == UINT32_MAX &&
               m->warmup_s == 0;
    if ((m->flags & ALARM_READY) == 0)
        return m->flags == 0 && m->motion_count == 0 && m->event_age_ms == UINT32_MAX;
    return m->warmup_s == 0;
}
uint16_t alarm_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = UINT16_C(0xffff);
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & UINT16_C(0x8000)) ? (uint16_t)((crc << 1) ^ UINT16_C(0x1021))
                                           : (uint16_t)(crc << 1);
    }
    return crc;
}
bool alarm_encode(const alarm_message_t *m, uint8_t *out, size_t capacity)
{
    if (m == NULL || out == NULL || capacity < ALARM_PACKET_SIZE || !valid(m))
        return false;
    out[0] = 'W';
    out[1] = 'A';
    out[2] = ALARM_PROTOCOL_VERSION;
    out[3] = (uint8_t)m->type;
    out[4] = (uint8_t)m->role;
    out[5] = m->flags;
    put32(out + 6, m->session);
    put32(out + 10, m->sequence);
    put32(out + 14, m->motion_count);
    put32(out + 18, m->event_age_ms);
    out[22] = (uint8_t)(m->warmup_s >> 8);
    out[23] = (uint8_t)m->warmup_s;
    uint16_t crc = alarm_crc16(out, 24);
    out[24] = (uint8_t)(crc >> 8);
    out[25] = (uint8_t)crc;
    return true;
}
bool alarm_decode(const uint8_t *data, size_t length, alarm_message_t *out)
{
    if (data == NULL || out == NULL || length != ALARM_PACKET_SIZE || data[0] != 'W' ||
        data[1] != 'A' || data[2] != ALARM_PROTOCOL_VERSION)
        return false;
    uint16_t crc = (uint16_t)(((uint16_t)data[24] << 8) | data[25]);
    if (alarm_crc16(data, 24) != crc)
        return false;
    alarm_message_t m = {
        .type = (alarm_message_type_t)data[3],
        .role = (alarm_role_t)data[4],
        .flags = data[5],
        .session = get32(data + 6),
        .sequence = get32(data + 10),
        .motion_count = get32(data + 14),
        .event_age_ms = get32(data + 18),
        .warmup_s = (uint16_t)(((uint16_t)data[22] << 8) | data[23]),
    };
    if (!valid(&m))
        return false;
    *out = m;
    return true;
}
alarm_message_t alarm_ack_for(const alarm_message_t *m, alarm_role_t role)
{
    alarm_message_t ack = {
        .type = ALARM_ACK, .role = role, .session = m->session, .sequence = m->sequence};
    return ack;
}
bool alarm_u32_newer(uint32_t value, uint32_t previous)
{
    uint32_t distance = value - previous;
    return distance != 0 && distance < UINT32_C(0x80000000);
}
alarm_rx_result_t alarm_rx_accept(alarm_rx_window_t *w, const alarm_message_t *m)
{
    if (!w->initialized) {
        w->initialized = true;
        w->session = m->session;
        w->sequence = m->sequence;
        return ALARM_RX_NEW_SESSION;
    }
    if (m->session != w->session) {
        for (unsigned i = 0; i < w->retired_count; ++i)
            if (w->retired[i] == m->session)
                return ALARM_RX_OLD;
        w->retired[w->retired_next] = w->session;
        w->retired_next = (w->retired_next + 1) % 4;
        if (w->retired_count < 4)
            ++w->retired_count;
        w->session = m->session;
        w->sequence = m->sequence;
        return ALARM_RX_NEW_SESSION;
    }
    if (m->sequence == w->sequence)
        return ALARM_RX_DUPLICATE;
    if (!alarm_u32_newer(m->sequence, w->sequence))
        return ALARM_RX_OLD;
    w->sequence = m->sequence;
    return ALARM_RX_FRESH;
}
