/* Tests the actual compositor with a scanout that keeps reading throughout
 * clears/glyph writes. A deferred cache flush alone would fail these checks. */
#include "../console_frame.h"
#include "../ui_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"render line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static xf_console_frame frame;
static unsigned char surface[2][32], visible[32];
static int front = -1, queued = -1, acknowledged, flushed = -1;
static unsigned clears, glyphs, flips;
static void immutable(unsigned buffer) {
    CHECK((int)buffer != front && (int)buffer != queued);
    if (front >= 0) CHECK(!memcmp(surface[front], visible, sizeof(visible)));
}
static void clear(void *ctx, unsigned buffer, uint32_t bg) {
    (void)ctx; (void)bg; immutable(buffer); memset(surface[buffer], ' ', 32); ++clears; flushed = -1;
}
static void glyph(void *ctx, unsigned buffer, unsigned x, unsigned y, const xf_console_cell *c) {
    (void)ctx; immutable(buffer); CHECK(x < 8 && y < 4);
    surface[buffer][y*8+x] = c->character; ++glyphs; flushed = -1;
}
static void flush(void *ctx, unsigned buffer) { (void)ctx; immutable(buffer); flushed = (int)buffer; }
static int flip(void *ctx, unsigned buffer) {
    (void)ctx; immutable(buffer); CHECK(flushed == (int)buffer); queued = (int)buffer; ++flips; return 1;
}
static int complete(void *ctx, unsigned buffer) {
    (void)ctx; CHECK((int)buffer == queued);
    if (acknowledged < 0) return -1;
    if (!acknowledged) return 0;
    front = queued; queued = -1; memcpy(visible, surface[front], sizeof(visible)); return 1;
}
static xf_console_backend backend = {clear, glyph, flush, flip, complete, NULL};
static void reset(void) {
    front = queued = flushed = -1; acknowledged = 0; clears = glyphs = flips = 0;
    CHECK(xf_console_frame_init(&frame, 8, 4, &backend));
}
static void capture(const char *s, uint32_t now) {
    CHECK(xf_console_frame_begin(&frame, 1, 2));
    while (*s) xf_console_frame_putch(&frame, (unsigned char)*s++, 1, 2);
    xf_console_frame_end(&frame, now);
}
static void ack(uint32_t now) { acknowledged = 1; xf_console_frame_poll(&frame, now); acknowledged = 0; CHECK(xf_console_frame_ready(&frame)); }
int main(void) {
    unsigned before, i, updates = 0;
    xf_ui_clock clock = {0,0};
    reset(); capture("STATIC\n123", 0);
    CHECK(clears == 1 && glyphs == 9 && flips == 1 && front == -1);
    CHECK(!xf_console_frame_begin(&frame, 1, 2));
    xf_console_frame_poll(&frame, 10); CHECK(front == -1 && !xf_console_frame_ready(&frame));
    ack(17); CHECK(!memcmp(visible,"STATIC  123",11));
    capture("STATIC\n124", 250); /* Cold second surface. */
    CHECK(clears == 2 && glyphs == 18 && !memcmp(visible,"STATIC  123",11)); ack(267);
    before = glyphs; capture("STATIC\n125", 500);
    CHECK(glyphs == before+1 && clears == 2); /* Static labels aren't painted again. */
    CHECK(!memcmp(visible,"STATIC  124",11)); ack(517); CHECK(!memcmp(visible,"STATIC  125",11));
    before = flips; capture("STATIC\n125", 750);
    CHECK(flips == before && frame.unchanged == 1 && xf_console_frame_ready(&frame));
    capture("STATIC\n1", 1000); ack(1017); CHECK(!memcmp(visible,"STATIC  1       ",16)); /* Shortened text erases hidden cells. */
    CHECK(clears == 2);
    CHECK(xf_console_frame_begin(&frame,1,2));
    xf_console_frame_putch(&frame,'S',1,3); xf_console_frame_end(&frame,1250); ack(1267);
    CHECK(visible[0]=='S' && frame.saved[frame.front][0].foreground==3);
    reset(); capture("one\ntwo\nthree\nfour\nfive",0); ack(17);
    CHECK(!memcmp(visible,"three   four    five",20)); /* Same bounded two-row scroll. */
    reset(); capture("pending",0xfffffff0U);
    xf_console_frame_poll(&frame,0x1e3U); CHECK(!frame.fault);
    xf_console_frame_poll(&frame,0x1e4U); CHECK(frame.fault == 3 && !xf_console_frame_ready(&frame));
    CHECK(!xf_console_frame_begin(&frame,1,2)); /* Timed-out scanout never reused. */
    reset(); capture("error",0); acknowledged=-1; xf_console_frame_poll(&frame,1); CHECK(frame.fault==3);
    CHECK(!xf_console_frame_init(&frame,241,4,&backend)); CHECK(!xf_console_frame_init(&frame,8,1,&backend));
    for(i=0;i<1000;++i) if(xf_ui_due(&clock,i,0,1)) { ++updates; xf_ui_rendered(&clock,i); }
    CHECK(updates==4); CHECK(!xf_ui_due(&clock,2000,0,0)); /* Static page has no timer redraw. */
    CHECK(xf_ui_due(&clock,751,1,0)); /* Input doesn't wait for statistics. */
    xf_ui_rendered(&clock,0xfffffff0U); CHECK(!xf_ui_due(&clock,0xe9U,0,1)); CHECK(xf_ui_due(&clock,0xeaU,0,1));
    printf("Complete-frame isolation, glyph reuse, flip fencing and UI clock: %u assertions passed\n", checks);
    return 0;
}
