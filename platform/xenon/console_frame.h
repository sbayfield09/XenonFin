#ifndef XF_CONSOLE_FRAME_H
#define XF_CONSOLE_FRAME_H
#include <stddef.h>
#include <stdint.h>
#define XF_CONSOLE_COLUMNS 240
#define XF_CONSOLE_ROWS 68
#define XF_CONSOLE_CELLS (XF_CONSOLE_COLUMNS * XF_CONSOLE_ROWS)
typedef struct { uint32_t background, foreground; unsigned char character; } xf_console_cell;
typedef struct {
    void (*clear)(void *, unsigned buffer, uint32_t background);
    void (*glyph)(void *, unsigned buffer, unsigned x, unsigned y, const xf_console_cell *);
    void (*flush)(void *, unsigned buffer);
    int (*flip)(void *, unsigned buffer);
    int (*complete)(void *, unsigned buffer); /* 1 acknowledged, 0 pending, -1 error */
    void *context;
} xf_console_backend;
typedef struct {
    xf_console_cell incoming[XF_CONSOLE_CELLS], saved[2][XF_CONSOLE_CELLS];
    xf_console_backend backend;
    unsigned columns, rows, x, y;
    uint32_t background, foreground, queued_at;
    int front, pending, active, valid[2], fault;
    unsigned frames, flips, glyphs, unchanged;
} xf_console_frame;
int xf_console_frame_init(xf_console_frame *, unsigned columns, unsigned rows, const xf_console_backend *);
int xf_console_frame_begin(xf_console_frame *, uint32_t background, uint32_t foreground);
void xf_console_frame_putch(xf_console_frame *, unsigned char, uint32_t background, uint32_t foreground);
void xf_console_frame_end(xf_console_frame *, uint32_t now);
void xf_console_frame_poll(xf_console_frame *, uint32_t now);
int xf_console_frame_ready(const xf_console_frame *);
#endif
