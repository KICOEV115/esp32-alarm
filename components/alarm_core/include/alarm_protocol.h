#ifndef ALARM_PROTOCOL_H
#define ALARM_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ALARM_PACKET_SIZE = 26, ALARM_PROTOCOL_VERSION = 3 };
typedef enum { ALARM_STATE = 1, ALARM_ACK = 2 } alarm_message_type_t;
typedef enum { ALARM_DETECTOR = 1, ALARM_PANEL = 2 } alarm_role_t;
enum { ALARM_READY = 1, ALARM_HIGH = 2 };
typedef struct {
    alarm_message_type_t type;
    alarm_role_t role;
    uint8_t flags;
    uint32_t session;
    uint32_t sequence;
    uint32_t motion_count;
    uint32_t event_age_ms; /* UINT32_MAX means no motion has occurred. */
    uint16_t warmup_s;
} alarm_message_t;

uint16_t alarm_crc16(const uint8_t *data, size_t length);
bool alarm_encode(const alarm_message_t *message, uint8_t *output, size_t capacity);
/* Exact length/version/CRC/field validation; output unchanged on failure. */
bool alarm_decode(const uint8_t *data, size_t length, alarm_message_t *output);
alarm_message_t alarm_ack_for(const alarm_message_t *message, alarm_role_t role);

/* Sequence numbers are compared modulo 2^32 within half the number space. */
bool alarm_u32_newer(uint32_t value, uint32_t previous);
typedef enum {
    ALARM_RX_FRESH,
    ALARM_RX_NEW_SESSION,
    ALARM_RX_DUPLICATE,
    ALARM_RX_OLD
} alarm_rx_result_t;
typedef struct {
    bool initialized;
    uint32_t session, sequence;
    uint32_t retired[4];
    unsigned retired_count, retired_next;
} alarm_rx_window_t;
alarm_rx_result_t alarm_rx_accept(alarm_rx_window_t *window, const alarm_message_t *message);
#endif
