#ifndef UI_FRAME_H
#define UI_FRAME_H
#include "alarm_logic.h"
enum { UI_WIDTH = 128, UI_HEIGHT = 64, UI_FRAME_BYTES = 1024 };
typedef alarm_panel_t ui_state_t;
uint8_t ui_dirty_pages(const uint8_t *frame, const uint8_t *previous, bool force);
void ui_frame_render(uint8_t frame[UI_FRAME_BYTES], const ui_state_t *state, int64_t now_ms);
#endif
