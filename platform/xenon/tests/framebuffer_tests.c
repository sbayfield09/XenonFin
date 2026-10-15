/* Real native presenter + compositor, with the original framebuffer mapped to
 * RAM. Any access to a GPU acknowledgement register fails this regression. */
#include "../framebuffer.h"
#include <xenos/xenos.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"framebuffer line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned char live[8*1024*1024] __attribute__((aligned(128)));
static unsigned width=1280, height=720, pitch=1280;
static int overscan;
static unsigned flushes, bytes_flushed, reads;
static xf_console_frame frame;
/* Two recognizable opposite glyph patterns; space is blank. */
const unsigned char fontdata_8x16[4096] = { ['A'*16]=0x80, ['A'*16+1]=0x01, ['B'*16]=0x01, ['B'*16+1]=0x80 };
uint32_t xenos_read32(int reg) {
    ++reads;
    switch (reg) {
        case D1GRPH_X_END: return width;
        case D1GRPH_Y_END: return height;
        case D1GRPH_PITCH: return pitch;
        case D1GRPH_PRIMARY_SURFACE_ADDRESS: return 0x01000000;
        default: CHECK(0); return 0; /* Counter/update/flip registers are forbidden. */
    }
}
/* No xenos_write32 definition: attempting a GPU flip must fail to link. */
void *xf_test_map_display(uint32_t address) { CHECK(address==0x01000000); return live; }
int xenos_is_overscan(void) { return overscan; }
void memdcbst(void *address, int size) {
    CHECK((uintptr_t)address>=(uintptr_t)live && (uintptr_t)address+(unsigned)size<=(uintptr_t)live+sizeof(live));
    CHECK(!((uintptr_t)address&127) && size>0 && !(size&127));
    ++flushes; bytes_flushed+=(unsigned)size;
}
static unsigned tile(unsigned x, unsigned y) {
    unsigned base=(y/32)*(32*pitch)+(x/32)*1024;
    return (base+x%4+(y%2)*4+((x%32)/4)*8+((y%32)/2)*64)^((y&8)*4);
}
static void capture(char value, uint32_t now) {
    unsigned i;
    CHECK(xf_console_frame_begin(&frame,0x11223344,0xffeeeeee));
    xf_console_frame_putch(&frame,'A',0x11223344,0xffeeeeee);
    for(i=0;i<7;++i) xf_console_frame_putch(&frame,' ',0x11223344,0xffeeeeee);
    xf_console_frame_putch(&frame,(unsigned char)value,0x11223344,0xffeeeeee);
    xf_console_frame_end(&frame,now);
    /* Poll after a long controller/network service gap. No retrace signal or
     * elapsed-time assumption is needed: copies are already complete. */
    xf_console_frame_poll(&frame,now+1000);
    CHECK(xf_console_frame_ready(&frame) && !frame.fault);
}
int main(void) {
    xf_console_backend backend;
    xf_console_cell cell = {0x11223344,0xffeeeeee,'A'};
    unsigned columns, rows, before, i; uint32_t *view=(uint32_t *)live;
    memset(live,0x55,sizeof(live));
    CHECK(xf_framebuffer_init(&backend,&columns,&rows)); CHECK(columns==160 && rows==45 && reads==4);
    backend.clear(NULL,0,cell.background); backend.glyph(NULL,0,2,3,&cell); backend.flush(NULL,0);
    CHECK(flushes==0 && view[tile(16,48)]==0x55555555); /* Hidden composition, no live clear. */
    CHECK(backend.flip(NULL,0) && backend.complete(NULL,0)==1);
    CHECK(bytes_flushed==1280*736*4);
    CHECK(view[tile(16,48)]==cell.foreground && view[tile(17,48)]==cell.background);
    CHECK(view[tile(23,49)]==cell.foreground && view[tile(16,49)]==cell.background);
    backend.clear(NULL,1,cell.background); cell.character='B'; backend.glyph(NULL,1,2,3,&cell);
    before=bytes_flushed; CHECK(view[tile(16,48)]==cell.foreground);
    CHECK(backend.flip(NULL,1) && backend.complete(NULL,1)==1);
    CHECK(bytes_flushed-before>0 && bytes_flushed-before<=4*128);
    CHECK(view[tile(16,48)]==cell.background && view[tile(23,48)]==cell.foreground);
    before=flushes; CHECK(backend.flip(NULL,1)); CHECK(flushes==before); /* Identical frame, no writes. */
    xf_framebuffer_shutdown();
    /* Integration: many actual compositor frames remain responsive while no
     * GPU frame-count/pending register is available or changes at all. */
    CHECK(xf_framebuffer_init(&backend,&columns,&rows));
    CHECK(xf_console_frame_init(&frame,columns,rows,&backend));
    capture('A',0); before=bytes_flushed;
    capture('B',250); CHECK(bytes_flushed-before<=4*128);
    CHECK(view[tile(0,0)]==0xffeeeeee && view[tile(64,0)]==0x11223344 && view[tile(71,0)]==0xffeeeeee);
    before=flushes; capture('B',500); CHECK(before==flushes && frame.unchanged==1);
    for(i=0;i<100;++i) capture(i&1?'A':'B',750+i*250);
    capture(' ',26000); CHECK(view[tile(64,0)]==0x11223344 && view[tile(71,0)]==0x11223344);
    CHECK(view[tile(0,0)]==0xffeeeeee); /* Static title survives changed/erased statistics. */
    CHECK(reads==8); /* Init reads only, however long playback/navigation runs. */
    {
        static uint32_t video_tiles[640*384]; unsigned vx,vy,vw,vh,footer,j;
        CHECK(xf_framebuffer_video_area(&vx,&vy,&vw,&vh,&footer));
        CHECK(vw==640 && vh==384 && vy==128 && footer==33);
        for(j=0;j<vw*vh;++j) video_tiles[j]=0xff224466;
        CHECK(xf_framebuffer_video_copy(video_tiles,vx,vy,vw,vh));
        CHECK(view[tile(vx,vy)]==0xff224466 && view[tile(0,0)]==0xffeeeeee);
        capture(' ',27000); CHECK(view[tile(vx,vy)]==0xff224466); /* Identical overlay. */
        capture('B',27250); CHECK(view[tile(vx,vy)]==0xff224466); /* Changed stats don't erase video. */
        xf_framebuffer_invalidate(); capture('A',27500);
        CHECK(view[tile(vx,vy)]==0x11223344); /* Stop/page change restores UI. */
        CHECK(!xf_framebuffer_video_copy(NULL,vx,vy,vw,vh));
        CHECK(!xf_framebuffer_video_copy(video_tiles,vx+1,vy,vw,vh));
        CHECK(!xf_framebuffer_video_copy(video_tiles,vx,vy,4096,vh));
    }
    overscan=1; CHECK(xf_framebuffer_init(&backend,&columns,&rows));
    CHECK(columns==(1280-2*(1280/28))/8 && rows==(720-2*(720/28))/16);
    backend.clear(NULL,0,cell.background); cell.character='A'; backend.glyph(NULL,0,0,0,&cell);
    CHECK(backend.flip(NULL,0)); CHECK(view[tile(1280/28,720/28)]==cell.foreground);
    overscan=0; width=pitch=1920; height=1080;
    CHECK(xf_framebuffer_init(&backend,&columns,&rows)); CHECK(columns==240 && rows==67);
    backend.clear(NULL,1,0); backend.glyph(NULL,1,239,66,&cell); before=bytes_flushed;
    CHECK(backend.flip(NULL,1)); CHECK(bytes_flushed-before==1920*1088*4);
    CHECK(view[tile(239*8,66*16)]==cell.foreground);
    {
        const unsigned sizes[][2]={{640,400},{640,480},{720,576},{1280,720},{1920,1080}};
        unsigned mode,scan,vx,vy,vw,vh,footer;
        for(mode=0;mode<5;++mode) for(scan=0;scan<2;++scan) {
            width=sizes[mode][0];height=sizes[mode][1];pitch=(width+31)&~31U;overscan=(int)scan;
            CHECK(xf_framebuffer_init(&backend,&columns,&rows));
            CHECK(xf_framebuffer_video_area(&vx,&vy,&vw,&vh,&footer));
            CHECK(footer+4<rows && !((vx|vy|vw|vh)&31) && vx+vw<=width && vy+vh<=height);
        }
    }
    pitch=1; CHECK(!xf_framebuffer_init(&backend,&columns,&rows));
    pitch=width=4096; CHECK(!xf_framebuffer_init(&backend,&columns,&rows));
    printf("Native compatibility presentation, changed pixels and unavailable GPU fence: %u assertions passed\n",checks);
    return 0;
}
