#ifndef XF_FRAMEBUFFER_H
#define XF_FRAMEBUFFER_H
#include "console_frame.h"
int xf_framebuffer_init(xf_console_backend *, unsigned *columns, unsigned *rows);
void xf_framebuffer_invalidate(void);
int xf_framebuffer_video_area(unsigned *x, unsigned *y, unsigned *width, unsigned *height, unsigned *text_row);
int xf_framebuffer_video_copy(const void *tiled, unsigned x, unsigned y, unsigned width, unsigned height);
void xf_framebuffer_shutdown(void);
#endif
