#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_frame.h"
int main(void)
{
    struct {
        uint8_t before[32], frame[UI_FRAME_BYTES], after[32];
    } guarded;
    for (unsigned mode = PANEL_DISARMED; mode <= PANEL_ALARM; ++mode)
        for (unsigned link = LINK_WAITING; link <= LINK_LOST; ++link)
            for (unsigned fault = FAULT_NONE; fault <= FAULT_COUNTER; ++fault) {
                memset(&guarded, 0xa5, sizeof(guarded));
                ui_state_t s = {.mode = (panel_mode_t)mode,
                                .link = (panel_link_t)link,
                                .fault = (panel_fault_t)fault,
                                .have_detector = true,
                                .sensor_ready = true,
                                .motion_count = UINT32_MAX,
                                .warmup_s = UINT16_MAX,
                                .arm_deadline_ms = 10000};
                ui_frame_render(guarded.frame, &s, 1);
                for (unsigned i = 0; i < 32; ++i)
                    assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
                for (unsigned x = 0; x < UI_WIDTH; ++x)
                    assert(guarded.frame[2 * UI_WIDTH + x] & 0x80);
            }
    ui_state_t s = {.mode = PANEL_ALARM,
                    .link = LINK_CONNECTED,
                    .have_detector = true,
                    .sensor_ready = true,
                    .motion_high = true};
    uint8_t high[UI_FRAME_BYTES], clear[UI_FRAME_BYTES], previous[UI_FRAME_BYTES] = {0};
    ui_frame_render(high, &s, 0);
    s.motion_high = false;
    ui_frame_render(clear, &s, 1000);
    assert(memcmp(high, clear, sizeof(high)) == 0); /* CLEAR never changes latched alarm screen. */
    assert(ui_dirty_pages(clear, clear, false) == 0);
    assert(ui_dirty_pages(clear, clear, true) == 0xff);
    memset(clear, 0, sizeof(clear));
    for (unsigned i = 0; i < UI_FRAME_BYTES; ++i) {
        clear[i] = 1;
        assert(ui_dirty_pages(clear, previous, false) == (1u << (i / UI_WIDTH)));
        clear[i] = 0;
    }
    s.mode = PANEL_ARMING;
    s.arm_deadline_ms = 10000;
    ui_frame_render(high, &s, 1000);
    ui_frame_render(clear, &s, 2000);
    assert(ui_dirty_pages(clear, high, false) == 0x40); /* Countdown touches detail page only. */
    puts("PASS OLED UI: alarm/control/link/fault combinations, bounds, persistent ALARM after "
         "CLEAR, page masks and countdown");
    return 0;
}
