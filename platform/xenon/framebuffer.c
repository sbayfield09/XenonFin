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

/* Compatibility presenter: complete CPU-side surfaces, changed cache-line
 * copies to the original libXenon framebuffer. No GPU flips or retrace fence:
 * 0.4.1's unverified acknowledgement could permanently suppress menu redraws. */
#include "framebuffer.h"
#include <xenos/xenos.h>
#include <ppc/cache.h>
#include <stdint.h>
#include <string.h>
extern const unsigned char fontdata_8x16[4096];
extern uint32_t xenos_read32(int);
#define XF_SURFACE_BYTES (8U * 1024U * 1024U)
#define XF_CACHE_LINE 128U
static uint32_t pixels[2][XF_SURFACE_BYTES / 4] __attribute__((aligned(65536)));
static unsigned width, height, pitch, padded_height, origin_x, origin_y;
static unsigned char *display;
static int presented = -1;
/* Cache lines touched since each source was last compared. Keep the new
 * front's bits until the next flip, so reverting a glyph is also detected. */
#define XF_DIRTY_WORDS (XF_SURFACE_BYTES / XF_CACHE_LINE / 32U)
static uint32_t dirty[2][XF_DIRTY_WORDS];
static void barrier(void) {
#ifdef XENON
    __asm__ volatile("sync" ::: "memory");
#endif
}
/* Same cached mapping as console_init(). Kept as a seam for hosted tests. */
#ifndef XF_FRAMEBUFFER_HOST_TEST
static void *map_display(uint32_t address) { return (void *)(uintptr_t)(address | 0x80000000U); }
#else
extern void *xf_test_map_display(uint32_t);
#define map_display xf_test_map_display
#endif
static void clear(void *context, unsigned buffer, uint32_t color) {
    unsigned i; (void)context;
    for (i = 0; i < pitch * padded_height; ++i) pixels[buffer][i] = color;
    memset(dirty[buffer],255,sizeof(dirty[buffer]));
}
static void glyph(void *context, unsigned buffer, unsigned column, unsigned row, const xf_console_cell *cell) {
    unsigned x, y; (void)context;
    for (y = 0; y < 16; ++y) for (x = 0; x < 8; ++x) {
        unsigned px = origin_x + column * 8 + x, py = origin_y + row * 16 + y;
        unsigned tile = (((py >> 5) * 32 * pitch + ((px >> 5) << 10) +
            (px & 3) + ((py & 1) << 2) + (((px & 31) >> 2) << 3) + (((py & 31) >> 1) << 6)) ^ ((py & 8) << 2));
        unsigned bit = (fontdata_8x16[(unsigned)cell->character * 16 + y] >> (7 - x)) & 1;
        pixels[buffer][tile] = bit ? cell->foreground : cell->background;
        dirty[buffer][tile / (32U*32U)] |= 1U << ((tile / 32U) & 31U);
    }
}
static void flush(void *context, unsigned buffer) {
    /* Sources are CPU-only cached memory; never scanned by the GPU. The copy
     * below flushes only completed destination regions, after their writes. */
    (void)context; (void)buffer;
}
static int flip(void *context, unsigned buffer) {
    const unsigned char *source = (const unsigned char *)pixels[buffer];
    unsigned size = pitch * padded_height * 4, offset;
    (void)context;
    if (presented < 0) {
        /* Initial background plus text, in one copy, with no erase-only pass. */
        memcpy(display, source, size); memdcbst(display, (int)size);
    } else {
        const unsigned char *previous = (const unsigned char *)pixels[presented];
        for (offset = 0; offset < size; offset += XF_CACHE_LINE) {
            unsigned line=offset/XF_CACHE_LINE;
            if (!((dirty[buffer][line/32] | dirty[presented][line/32]) & (1U << (line&31)))) continue;
            if (memcmp(source + offset, previous + offset, XF_CACHE_LINE)) {
                /* Write finished glyph pixels directly, including spaces that
                 * erase shortened values. Unchanged text is never touched. */
                memcpy(display + offset, source + offset, XF_CACHE_LINE);
                memdcbst(display + offset, XF_CACHE_LINE);
            }
        }
    }
    if(presented>=0 && presented!=(int)buffer) memset(dirty[presented],0,sizeof(dirty[presented]));
    barrier(); presented = (int)buffer; return 1;
}
static int complete(void *context, unsigned buffer) {
    (void)context;
    /* Presentation finished synchronously, without a display-register wait. */
    return presented == (int)buffer ? 1 : -1;
}
int xf_framebuffer_init(xf_console_backend *backend, unsigned *columns, unsigned *rows) {
    uint32_t address;
    width = xenos_read32(D1GRPH_X_END); height = xenos_read32(D1GRPH_Y_END); pitch = xenos_read32(D1GRPH_PITCH);
    if (!width || width > 1920 || height < 32 || height > 1080 || pitch != ((width + 31) & ~31U)) return 0;
    padded_height = (height + 31) & ~31U;
    if ((uint64_t)pitch * padded_height * 4 > XF_SURFACE_BYTES) return 0;
    address = xenos_read32(D1GRPH_PRIMARY_SURFACE_ADDRESS);
    if (address & (XF_CACHE_LINE - 1)) return 0;
    display = map_display(address);
    if (!display) return 0;
    origin_x = origin_y = 0;
    if (xenos_is_overscan()) { origin_x = width / 28; origin_y = height / 28; }
    *columns = (width - 2 * origin_x) / 8; *rows = (height - 2 * origin_y) / 16;
    backend->clear = clear; backend->glyph = glyph; backend->flush = flush; backend->flip = flip; backend->complete = complete; backend->context = 0;
    memset(dirty,0,sizeof(dirty)); presented = -1; return 1;
}
void xf_framebuffer_shutdown(void) { /* Original video mode/address never changed. */ }

void xf_framebuffer_invalidate(void) { presented=-1; }
int xf_framebuffer_video_area(unsigned *x, unsigned *y, unsigned *w, unsigned *h, unsigned *text_row) {
    unsigned available, rows=(height-2*origin_y)/16;
    if(!display || width<640 || height<400 || width<2*origin_x+96) return 0;
    *y=(origin_y+112+31)&~31U;
    if(height<=origin_y+*y+96) return 0;
    available=(width-2*origin_x-64)&~31U; *w=available>640?640:available;
    available=(height-origin_y-*y-96)&~31U; *h=available>384?384:available;
    if(*w<32 || *h<32) return 0;
    *x=((width-*w)/2)&~31U;
    *text_row=(*y+*h-origin_y+15)/16+1;
    /* Four diagnostic lines plus their final newline must not trigger the
     * console's two-row scroll in short/overscanned display modes. */
    while(*h>=32 && *text_row+4>=rows) { *h-=32; *text_row=(*y+*h-origin_y+15)/16+1; }
    return *h>=32;
}
int xf_framebuffer_video_copy(const void *tiled, unsigned x, unsigned y, unsigned w, unsigned h) {
    const unsigned char *source=tiled; unsigned row;
    if(!display || !tiled || !w || !h || ((x|y|w|h)&31) || x>width || w>width-x || y>height || h>height-y) return 0;
    /* A complete 32-row tile band is contiguous. Copy finished tile bands,
     * never issue per-pixel writes to the scanned framebuffer. */
    for(row=0;row<h;row+=32) {
        unsigned char *destination=display+(((y+row)*pitch+x*32)*4);
        memcpy(destination,source+row*w*4,w*32*4); memdcbst(destination,(int)(w*32*4));
    }
    barrier(); return 1;
}
