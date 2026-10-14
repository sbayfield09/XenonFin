/* Compile the real tiled/native scanout backend with only MMIO/cache mocked. */
#include "../framebuffer.h"
#include <xenos/xenos.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"framebuffer line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static uint32_t registers[0x7000/4];
static uint32_t *flushed;
static int flush_size, overscan;
static unsigned flushes, writes;
static int advance_on_write;
/* A has one lit pixel at each edge, on alternate rows. */
const unsigned char fontdata_8x16[4096] = { ['A'*16]=0x80, ['A'*16+1]=0x01 };
uint32_t xenos_read32(int reg) { CHECK(reg>=0 && reg<0x7000 && !(reg&3)); return registers[reg/4]; }
void xenos_write32(int reg, uint32_t value) { CHECK(reg>=0 && reg<0x7000 && !(reg&3)); registers[reg/4]=value; ++writes; if (advance_on_write && reg==D1GRPH_PRIMARY_SURFACE_ADDRESS) ++registers[0x60a4/4]; }
int xenos_is_overscan(void) { return overscan; }
void memdcbst(void *address, int size) { flushed = address; flush_size=size; ++flushes; }
static unsigned tile(unsigned x, unsigned y, unsigned pitch) {
    /* Independently expressed mapping from libXenon's console_pset32. */
    unsigned base=(y/32)*(32*pitch)+(x/32)*1024;
    return (base+x%4+(y%2)*4+((x%32)/4)*8+((y%32)/2)*64)^((y&8)*4);
}
int main(void) {
    xf_console_backend backend;
    xf_console_cell cell = {0x11223344,0xffeeeeee,'A'};
    unsigned columns, rows; uint32_t *first, old;
    registers[D1GRPH_X_END/4]=1280; registers[D1GRPH_Y_END/4]=720; registers[D1GRPH_PITCH/4]=1280;
    registers[D1GRPH_PRIMARY_SURFACE_ADDRESS/4]=0x01000000; registers[D1GRPH_FLIP_CONTROL/4]=5;
    CHECK(xf_framebuffer_init(&backend,&columns,&rows)); CHECK(columns==160 && rows==45 && writes==0);
    backend.clear(NULL,0,cell.background); backend.glyph(NULL,0,2,3,&cell);
    CHECK(flushes==0); backend.flush(NULL,0); first=flushed;
    CHECK(flush_size==1280*736*4 && !((uintptr_t)first&65535));
    CHECK(first[tile(16,48,1280)]==cell.foreground);
    CHECK(first[tile(17,48,1280)]==cell.background);
    CHECK(first[tile(23,49,1280)]==cell.foreground);
    CHECK(first[tile(16,49,1280)]==cell.background);
    CHECK(first[tile(0,0,1280)]==cell.background);
    registers[D1GRPH_UPDATE/4]=4; CHECK(!backend.flip(NULL,0)); CHECK(writes==0);
    registers[D1GRPH_UPDATE/4]=0; registers[0x60a4/4]=9;
    advance_on_write=1; /* A retrace races the posted address write. */
    CHECK(backend.flip(NULL,0)); advance_on_write=0; CHECK(registers[D1GRPH_FLIP_CONTROL/4]==4);
    CHECK(registers[D1GRPH_PRIMARY_SURFACE_ADDRESS/4]==((uintptr_t)first&0x1fffffffU));
    CHECK(!backend.complete(NULL,0)); registers[0x60a4/4]=11; registers[D1GRPH_UPDATE/4]=4;
    CHECK(!backend.complete(NULL,0)); registers[D1GRPH_UPDATE/4]=0; CHECK(backend.complete(NULL,0)==1);
    old=first[tile(16,48,1280)]; backend.clear(NULL,1,0); backend.flush(NULL,1);
    CHECK(flushed!=first && first[tile(16,48,1280)]==old); /* Backbuffer is distinct. */
    CHECK(backend.complete(NULL,1)==-1);
    xf_framebuffer_shutdown(); CHECK(registers[D1GRPH_PRIMARY_SURFACE_ADDRESS/4]==0x01000000 && registers[D1GRPH_FLIP_CONTROL/4]==5);
    overscan=1; CHECK(xf_framebuffer_init(&backend,&columns,&rows)); CHECK(columns==(1280-2*(1280/28))/8 && rows==(720-2*(720/28))/16);
    backend.clear(NULL,0,cell.background); backend.glyph(NULL,0,0,0,&cell); backend.flush(NULL,0);
    CHECK(flushed[tile(1280/28,720/28,1280)]==cell.foreground);
    xf_framebuffer_shutdown(); overscan=0;
    registers[D1GRPH_X_END/4]=1920; registers[D1GRPH_Y_END/4]=1080; registers[D1GRPH_PITCH/4]=1920;
    CHECK(xf_framebuffer_init(&backend,&columns,&rows)); CHECK(columns==240 && rows==67);
    backend.clear(NULL,1,0); backend.glyph(NULL,1,239,66,&cell); backend.flush(NULL,1); CHECK(flush_size==1920*1088*4);
    xf_framebuffer_shutdown(); registers[D1GRPH_PITCH/4]=1; CHECK(!xf_framebuffer_init(&backend,&columns,&rows));
    registers[D1GRPH_PITCH/4]=4096; registers[D1GRPH_X_END/4]=4096; CHECK(!xf_framebuffer_init(&backend,&columns,&rows));
    printf("Native tiled pixels, bounded modes, cache flush and retrace registers: %u assertions passed\n",checks);
    return 0;
}
