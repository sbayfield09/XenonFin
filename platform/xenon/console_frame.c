/* A complete character frame is captured before touching ANY pixel surface.
 * Only the free backbuffer is changed. The visible/pending buffer is immutable. */
#include "console_frame.h"
#include <string.h>
static int same(const xf_console_cell *a, const xf_console_cell *b) {
    return a->character == b->character && a->background == b->background && a->foreground == b->foreground;
}
static void blank(xf_console_frame *f, xf_console_cell *c) {
    c->character = ' '; c->background = f->background; c->foreground = f->foreground;
}
int xf_console_frame_init(xf_console_frame *f, unsigned columns, unsigned rows, const xf_console_backend *backend) {
    memset(f, 0, sizeof(*f)); f->front = f->pending = -1;
    if (!columns || columns > XF_CONSOLE_COLUMNS || rows < 2 || rows > XF_CONSOLE_ROWS ||
        !backend || !backend->clear || !backend->glyph || !backend->flush || !backend->flip || !backend->complete) {
        f->fault = 1; return 0;
    }
    f->columns = columns; f->rows = rows; f->backend = *backend; return 1;
}
int xf_console_frame_ready(const xf_console_frame *f) { return !f->fault && !f->active && f->pending < 0; }
int xf_console_frame_begin(xf_console_frame *f, uint32_t background, uint32_t foreground) {
    unsigned i;
    if (!xf_console_frame_ready(f)) return 0;
    f->background = background; f->foreground = foreground; f->x = f->y = 0;
    for (i = 0; i < f->columns * f->rows; ++i) blank(f, &f->incoming[i]);
    f->active = 1; return 1;
}
static void newline(xf_console_frame *f) {
    f->x = 0;
    if (++f->y >= f->rows) {
        unsigned i, n = f->columns * (f->rows - 2);
        /* Preserve the old console's two-row scroll, but only in hidden cells. */
        memmove(f->incoming, f->incoming + 2 * f->columns, n * sizeof(*f->incoming));
        for (i = n; i < f->columns * f->rows; ++i) blank(f, &f->incoming[i]);
        f->y -= 2;
    }
}
void xf_console_frame_putch(xf_console_frame *f, unsigned char c, uint32_t background, uint32_t foreground) {
    xf_console_cell *cell;
    if (!f->active) return;
    if (c == '\r') { f->x = 0; return; }
    if (c == '\n') { newline(f); return; }
    if (c < 32 || c == 127) c = ' ';
    cell = &f->incoming[f->y * f->columns + f->x];
    cell->character = c; cell->background = background; cell->foreground = foreground;
    if (++f->x == f->columns) newline(f);
}
void xf_console_frame_end(xf_console_frame *f, uint32_t now) {
    unsigned i, buffer, count = f->columns * f->rows;
    int different = f->front < 0;
    if (!f->active) return;
    f->active = 0; ++f->frames;
    if (!different) for (i = 0; i < count; ++i) {
        if (!same(&f->incoming[i], &f->saved[f->front][i])) { different = 1; break; }
    }
    if (!different) { ++f->unchanged; return; }
    buffer = f->front == 0 ? 1 : 0;
    if (!f->valid[buffer]) f->backend.clear(f->backend.context, buffer, f->background);
    for (i = 0; i < count; ++i) {
        xf_console_cell *c = &f->incoming[i];
        if (!f->valid[buffer] || !same(c, &f->saved[buffer][i])) {
            if (f->valid[buffer] || c->character != ' ' || c->background != f->background) {
                f->backend.glyph(f->backend.context, buffer, i % f->columns, i / f->columns, c); ++f->glyphs;
            }
            f->saved[buffer][i] = *c;
        }
    }
    f->valid[buffer] = 1;
    f->backend.flush(f->backend.context, buffer); /* All glyphs/erasures precede visibility. */
    if (!f->backend.flip(f->backend.context, buffer)) { f->fault = 2; return; }
    f->pending = (int)buffer; f->queued_at = now;
}
void xf_console_frame_poll(xf_console_frame *f, uint32_t now) {
    int complete;
    if (f->pending < 0 || f->fault) return;
    complete = f->backend.complete(f->backend.context, (unsigned)f->pending);
    if (complete > 0) { f->front = f->pending; f->pending = -1; ++f->flips; }
    else if (complete < 0 || (uint32_t)(now - f->queued_at) >= 500) {
        /* Never guess that the former frontbuffer is safe to reuse. */
        f->fault = 3;
    }
}
