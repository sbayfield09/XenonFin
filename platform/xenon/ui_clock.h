#ifndef XF_UI_CLOCK_H
#define XF_UI_CLOCK_H
#include <stdint.h>
#define XF_UI_INTERVAL_MS 250U
typedef struct { uint32_t last_frame; int initialized; } xf_ui_clock;
int xf_ui_due(const xf_ui_clock *, uint32_t now, int event, int statistics);
void xf_ui_rendered(xf_ui_clock *, uint32_t now);
#endif
