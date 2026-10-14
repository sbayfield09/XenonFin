#ifndef XF_FRAMEBUFFER_H
#define XF_FRAMEBUFFER_H
#include "console_frame.h"
int xf_framebuffer_init(xf_console_backend *, unsigned *columns, unsigned *rows);
void xf_framebuffer_shutdown(void);
#endif
