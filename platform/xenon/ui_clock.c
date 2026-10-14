#include "ui_clock.h"
int xf_ui_due(const xf_ui_clock *clock, uint32_t now, int event, int statistics) {
    return event || !clock->initialized || (statistics && (uint32_t)(now - clock->last_frame) >= XF_UI_INTERVAL_MS);
}
void xf_ui_rendered(xf_ui_clock *clock, uint32_t now) { clock->last_frame = now; clock->initialized = 1; }
