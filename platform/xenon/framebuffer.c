// Copyright 2009  Georg Lukas <georg@op-co.de>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//
// hacked to fit into libxenon by Felix Domke <tmbinc@elitedvb.net>
// This is a bit ugly, and it's not Georg's fault.

/* Off-screen scanout adapter: pinned console font, tiling, colors and overscan. */
#include "framebuffer.h"
#include <xenos/xenos.h>
#include <ppc/cache.h>
#include <stdint.h>
extern const unsigned char fontdata_8x16[4096];
extern uint32_t xenos_read32(int);
extern void xenos_write32(int, uint32_t);
/* AVIVO scanout registers/bits: r500_reg.h and the existing Xenos display block. */
#define XF_FRAME_COUNT 0x60a4
#define XF_SURFACE_PENDING (1U << 2)
#define XF_H_RETRACE (1U << 0)
#define XF_SURFACE_BYTES (8U * 1024U * 1024U)
static uint32_t pixels[2][XF_SURFACE_BYTES / 4] __attribute__((aligned(65536)));
static unsigned width, height, pitch, padded_height, origin_x, origin_y;
static uint32_t boot_address, old_flip_control, queued_frame;
static int initialized;
static uint32_t physical(unsigned buffer) { return (uint32_t)((uintptr_t)pixels[buffer] & 0x1fffffffU); }
static void barrier(void) {
#ifdef XENON
    __asm__ volatile("sync" ::: "memory");
#endif
}
static void clear(void *context, unsigned buffer, uint32_t color) {
    unsigned i; (void)context;
    for (i = 0; i < pitch * padded_height; ++i) pixels[buffer][i] = color;
}
static void glyph(void *context, unsigned buffer, unsigned column, unsigned row, const xf_console_cell *cell) {
    unsigned x, y; (void)context;
    for (y = 0; y < 16; ++y) for (x = 0; x < 8; ++x) {
        unsigned px = origin_x + column * 8 + x, py = origin_y + row * 16 + y;
        unsigned tile = (((py >> 5) * 32 * pitch + ((px >> 5) << 10) +
            (px & 3) + ((py & 1) << 2) + (((px & 31) >> 2) << 3) + (((py & 31) >> 1) << 6)) ^ ((py & 8) << 2));
        unsigned bit = (fontdata_8x16[(unsigned)cell->character * 16 + y] >> (7 - x)) & 1;
        pixels[buffer][tile] = bit ? cell->foreground : cell->background;
    }
}
static void flush(void *context, unsigned buffer) {
    (void)context; memdcbst(pixels[buffer], (int)(pitch * padded_height * 4)); barrier();
}
static int flip(void *context, unsigned buffer) {
    (void)context;
    if (xenos_read32(D1GRPH_UPDATE) & XF_SURFACE_PENDING) return 0;
    /* Bit 0 selects horizontal/immediate flips in the old mode setup. Clear
     * it for vertical-retrace updates; don't wait/spin for a narrow vblank. */
    xenos_write32(D1GRPH_FLIP_CONTROL, old_flip_control & ~XF_H_RETRACE);
    xenos_write32(D1GRPH_PRIMARY_SURFACE_ADDRESS, physical(buffer));
    barrier(); (void)xenos_read32(D1GRPH_PRIMARY_SURFACE_ADDRESS);
    /* Snapshot AFTER the posted write: a retrace between the address write
     * and readback must not count as our future completion fence. Holding
     * the old front for an extra retrace is safe and never blocks the CPU. */
    queued_frame = xenos_read32(XF_FRAME_COUNT); return 1;
}
static int complete(void *context, unsigned buffer) {
    (void)context;
    if (xenos_read32(D1GRPH_PRIMARY_SURFACE_ADDRESS) != physical(buffer)) return -1;
    if ((xenos_read32(D1GRPH_UPDATE) & XF_SURFACE_PENDING) || xenos_read32(XF_FRAME_COUNT) == queued_frame) return 0;
    /* A retrace occurred AND the surface update finished. Old front is free. */
    return 1;
}
int xf_framebuffer_init(xf_console_backend *backend, unsigned *columns, unsigned *rows) {
    width = xenos_read32(D1GRPH_X_END); height = xenos_read32(D1GRPH_Y_END); pitch = xenos_read32(D1GRPH_PITCH);
    if (!width || width > 1920 || height < 32 || height > 1080 || pitch != ((width + 31) & ~31U)) return 0;
    padded_height = (height + 31) & ~31U;
    if ((uint64_t)pitch * padded_height * 4 > XF_SURFACE_BYTES) return 0;
    origin_x = origin_y = 0;
    if (xenos_is_overscan()) { origin_x = width / 28; origin_y = height / 28; }
    *columns = (width - 2 * origin_x) / 8; *rows = (height - 2 * origin_y) / 16;
    boot_address = xenos_read32(D1GRPH_PRIMARY_SURFACE_ADDRESS); old_flip_control = xenos_read32(D1GRPH_FLIP_CONTROL);
    backend->clear = clear; backend->glyph = glyph; backend->flush = flush; backend->flip = flip; backend->complete = complete; backend->context = 0;
    initialized = 1; return 1;
}
void xf_framebuffer_shutdown(void) {
    if (!initialized) return;
    xenos_write32(D1GRPH_FLIP_CONTROL, old_flip_control);
    xenos_write32(D1GRPH_PRIMARY_SURFACE_ADDRESS, boot_address); barrier(); initialized = 0;
}
