#include "ui_frame.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    char character;
    uint8_t columns[5];
} glyph_t;
static const glyph_t glyphs[] = {
    {'A', {0x7e, 0x9, 0x9, 0x9, 0x7e}},    {'B', {0x7f, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3e, 0x41, 0x41, 0x41, 0x41}}, {'D', {0x7f, 0x41, 0x41, 0x41, 0x3e}},
    {'E', {0x7f, 0x49, 0x49, 0x49, 0x41}}, {'F', {0x7f, 0x9, 0x9, 0x9, 0x1}},
    {'G', {0x3e, 0x41, 0x49, 0x49, 0x79}}, {'H', {0x7f, 0x8, 0x8, 0x8, 0x7f}},
    {'I', {0x41, 0x41, 0x7f, 0x41, 0x41}}, {'J', {0x30, 0x40, 0x41, 0x3f, 0x1}},
    {'K', {0x7f, 0x8, 0x14, 0x22, 0x41}},  {'L', {0x7f, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7f, 0x2, 0x4, 0x2, 0x7f}},    {'N', {0x7f, 0x2, 0x4, 0x8, 0x7f}},
    {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}}, {'P', {0x7f, 0x9, 0x9, 0x9, 0x6}},
    {'Q', {0x3e, 0x41, 0x51, 0x21, 0x5e}}, {'R', {0x7f, 0x9, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x1, 0x1, 0x7f, 0x1, 0x1}},
    {'U', {0x3f, 0x40, 0x40, 0x40, 0x3f}}, {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
    {'W', {0x7f, 0x20, 0x10, 0x20, 0x7f}}, {'X', {0x63, 0x14, 0x8, 0x14, 0x63}},
    {'Y', {0x3, 0x4, 0x78, 0x4, 0x3}},     {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'0', {0x3e, 0x51, 0x49, 0x45, 0x3e}}, {'1', {0x0, 0x42, 0x7f, 0x40, 0x0}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}}, {'3', {0x41, 0x49, 0x49, 0x49, 0x36}},
    {'4', {0x18, 0x14, 0x12, 0x7f, 0x10}}, {'5', {0x4f, 0x49, 0x49, 0x49, 0x31}},
    {'6', {0x3e, 0x49, 0x49, 0x49, 0x30}}, {'7', {0x1, 0x71, 0x9, 0x5, 0x3}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x6, 0x49, 0x49, 0x49, 0x3e}},
    {':', {0x0, 0x0, 0x36, 0x0, 0x0}},     {'-', {0x8, 0x8, 0x8, 0x8, 0x8}},
    {' ', {0x0, 0x0, 0x0, 0x0, 0x0}},
};

static void pixel(uint8_t *frame, int x, int y)
{
    if (x >= 0 && x < UI_WIDTH && y >= 0 && y < UI_HEIGHT) {
        frame[(y / 8) * UI_WIDTH + x] |= (uint8_t)(1u << (y % 8));
    }
}

static void text(uint8_t *frame, int x, int y, const char *value, unsigned scale)
{
    for (; *value != '\0' && x < UI_WIDTH; ++value, x += (int)(6 * scale)) {
        const uint8_t *columns = NULL;
        for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
            if (glyphs[i].character == *value) {
                columns = glyphs[i].columns;
                break;
            }
        }
        if (columns == NULL) {
            continue;
        }
        for (unsigned col = 0; col < 5; ++col) {
            for (unsigned row = 0; row < 7; ++row) {
                if ((columns[col] & (1u << row)) == 0) {
                    continue;
                }
                for (unsigned dx = 0; dx < scale; ++dx) {
                    for (unsigned dy = 0; dy < scale; ++dy) {
                        pixel(frame, x + (int)(col * scale + dx), y + (int)(row * scale + dy));
                    }
                }
            }
        }
    }
}

void ui_frame_render(uint8_t frame[UI_FRAME_BYTES], const ui_state_t *p, int64_t now_ms)
{
    memset(frame, 0, UI_FRAME_BYTES);
    text(frame, 3, 2, "WIRELESS ALARM", 1);
    text(frame, 3, 13,
         p->link == LINK_CONNECTED ? "LINK: ONLINE"
         : p->link == LINK_LOST    ? "LINK: OFFLINE"
                                   : "LINK: WAITING",
         1);
    for (int x = 0; x < UI_WIDTH; ++x)
        pixel(frame, x, 23);
    const char *title = alarm_panel_mode_name(p->mode);
    char detail[24] = {0};
    const char *hint = "PRESS ARM";
    if (p->mode == PANEL_ALARM) {
        snprintf(detail, sizeof(detail), "MOTION COUNT: %lu",
                 (unsigned long)(p->motion_count > 999999 ? 999999 : p->motion_count));
        hint = "PRESS DISARM";
    } else if (p->fault != FAULT_NONE) {
        title = "FAULT";
        snprintf(detail, sizeof(detail), "%s",
                 p->fault == FAULT_RESTART ? "DETECTOR RESTART" : "COUNTER FAULT");
        hint = "DISARM THEN REARM";
    } else if (p->link == LINK_LOST) {
        title = "LINK FAULT";
        snprintf(detail, sizeof(detail), "CHECK DETECTOR");
        hint = p->mode == PANEL_ARMED ? "MODE: ARMED" : "MODE: DISARMED";
    } else if (!p->have_detector) {
        snprintf(detail, sizeof(detail), "WAIT FOR DETECTOR");
        hint = "ARM DISABLED";
    } else if (!p->sensor_ready) {
        snprintf(detail, sizeof(detail), "PIR READY IN: %uS", p->warmup_s);
        hint = "ARM DISABLED";
    } else if (p->link != LINK_CONNECTED) {
        snprintf(detail, sizeof(detail), "WAIT FOR LINK ACK");
        hint = "ARM DISABLED";
    } else if (p->mode == PANEL_ARMING) {
        snprintf(detail, sizeof(detail), "EXIT IN: %uS", alarm_panel_countdown_s(p, now_ms));
        hint = "DISARM TO CANCEL";
    } else if (p->mode == PANEL_ARMED) {
        snprintf(detail, sizeof(detail), "MONITORING ENTRANCE");
        hint = "DISARM TO STOP";
    } else
        snprintf(detail, sizeof(detail), "PIR: %s", p->motion_high ? "HIGH" : "CLEAR");
    text(frame, 3, 28, title, 2);
    text(frame, 3, 46, detail, 1);
    text(frame, 3, 56, hint, 1);
}
uint8_t ui_dirty_pages(const uint8_t *frame, const uint8_t *previous, bool force)
{
    if (force)
        return UINT8_C(0xff);
    uint8_t mask = 0;
    for (unsigned page = 0; page < 8; ++page)
        if (memcmp(frame + page * UI_WIDTH, previous + page * UI_WIDTH, UI_WIDTH) != 0)
            mask |= (uint8_t)(1u << page);
    return mask;
}
