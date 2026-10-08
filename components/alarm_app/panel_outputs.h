#ifndef PANEL_OUTPUTS_H
#define PANEL_OUTPUTS_H
#include "alarm_logic.h"
#include "esp_err.h"
esp_err_t panel_outputs_start(void);
/* Called by the control task. Only a latched alarm turns the LED on. */
void panel_outputs_update(const alarm_panel_t *panel);
#endif
