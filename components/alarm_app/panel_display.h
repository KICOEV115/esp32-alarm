#ifndef PANEL_DISPLAY_H
#define PANEL_DISPLAY_H
#include "esp_err.h"
#include "ui_frame.h"
esp_err_t panel_display_start(void);
/* Nonblocking latest-state mailbox; called by the application task only. */
void panel_display_publish(const ui_state_t *state);
#endif
