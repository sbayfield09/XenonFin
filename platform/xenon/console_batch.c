/* The pinned console flushes its ENTIRE framebuffer after EVERY character.
 * Batch that exact buffer's cache stores for one draw. NIC/audio buffers always
 * pass through. No renderer, framebuffer layout or graphics init is replaced. */
#include "console_batch.h"
#include <stddef.h>
extern void console_clrscr(void);
extern void __real_memdcbst(void *, int);
static void *framebuffer;
static int framebuffer_size, capture, batching, dirty;
void __wrap_memdcbst(void *address, int size) {
    if (capture) { framebuffer = address; framebuffer_size = size; capture = 0; }
    if (batching && address == framebuffer && size == framebuffer_size) { dirty = 1; return; }
    __real_memdcbst(address, size);
}
void xf_console_begin(void) {
    batching = capture = 1; dirty = 0;
    console_clrscr(); /* Its first/only flush binds the actual console buffer. */
    capture = 0;
}
void xf_console_end(void) {
    batching = 0;
    if (dirty) __real_memdcbst(framebuffer, framebuffer_size);
    dirty = 0;
}
