#define _POSIX_C_SOURCE 200809L
#include "../video.h"
#include "../video_pixels.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"video line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static xf_video player;
static unsigned char source[480];
static uint64_t micro,ready_at,pending_pts,last_pts,last_display,first_display;
static unsigned calls,frames,submissions,displays,begins,ends,blocked,mode;
static unsigned decode_cost=600,prepare_cost=100,gpu_cost,copy_cost;
static double fps=25;
static int open_codec(unsigned char *data,size_t size,xf_video_info *info,char *error,size_t cap) {
    (void)data;(void)size;(void)error;(void)cap; calls=0;
    info->width=info->height=16;info->fps=fps;info->pixel_aspect=1; return 1;
}
static int next_codec(xf_video_image *image,char *error,size_t cap) {
    if(calls==frames) return 0;
    if(mode==4) {snprintf(error,cap,"decoder fault");return -1;}
    memset(source,(int)(calls%200+16),sizeof(source));
    image->width=image->height=16;image->y=source;image->cb=source+320;image->cr=source+400;
    image->y_stride=20;image->c_stride=10;image->pts_us=(uint64_t)(calls*1000000.0/fps+0.5); ++calls;micro+=decode_cost;return 1;
}
static void close_codec(void) {}
static const xf_video_codec codec={"fake timing codec",open_codec,next_codec,close_codec};
static int begin(void *context,const xf_video_info *info,char *error,size_t cap) {
    (void)context;CHECK(info->width==16 || info->width==320);++begins;
    if(mode==1) {snprintf(error,cap,"begin fault");return 0;} return 1;
}
static int submit(void *context,const xf_video_image *image,char *error,size_t cap) {
    (void)context;
    if(mode==2) {snprintf(error,cap,"submit fault");return 0;}
    if(!player.synthetic) {
        CHECK(image->y_stride==16 && image->c_stride==8);
        CHECK(image->y[0]==(unsigned)(image->pts_us*fps/1000000.0+0.5)%200+16 && image->cb[0]==image->y[0] && image->cr[0]==image->y[0]);
        CHECK(!submissions || image->pts_us>last_pts);
    }
    last_pts=pending_pts=image->pts_us;++submissions;micro+=prepare_cost;ready_at=micro+gpu_cost;return 1;
}
static int poll(void *context,char *error,size_t cap) {
    (void)context;if(mode==3) {snprintf(error,cap,"poll fault");return -1;}return !blocked && micro>=ready_at;
}
static int present(void *context,char *error,size_t cap) {
    (void)context;
    if(mode==5) {snprintf(error,cap,"present fault");return 0;}
    CHECK(pending_pts<=player.timeline_us || !player.clock_started);
    CHECK(!displays || pending_pts>last_display);last_display=pending_pts;
    if(!displays) first_display=micro;
    ++displays;micro+=copy_cost;return 1;
}
static void end(void *context) {(void)context;++ends;}
static uint64_t clock_us(void *context) {(void)context;return micro;}
static xf_video_backend backend={begin,submit,poll,end,clock_us,"mock",NULL,present,NULL};
static void tick(void) { xf_video_tick(&player,(uint32_t)(micro/1000)); }
static void step(unsigned wait) { tick(); micro+=wait; }
static void reset(unsigned count) {
    xf_video_stop(&player); xf_video_init(&player,&codec);frames=count;submissions=displays=begins=ends=blocked=mode=0;
    last_pts=last_display=0;micro=0;fps=25;decode_cost=600;prepare_cost=100;gpu_cost=copy_cost=0;
}
static void pixels_test(void) {
    /* Independently published limited BT.601 vectors for known RGB patches.
     * Padding is poison: a wrong plane address/stride/subsampling fails. */
    static const unsigned char yuv[8][3]={{16,128,128},{235,128,128},{81,90,240},{145,54,34},{41,240,110},{170,166,16},{106,202,222},{210,16,146}};
    static const unsigned char rgb[8][3]={{0,0,0},{255,255,255},{255,0,0},{0,255,0},{0,0,255},{0,255,255},{255,0,255},{255,255,0}};
    unsigned char y[20*16],cb[10*8],cr[10*8];uint32_t tiled[32*32];unsigned i,x,row;
    xf_video_image image={y,cb,cr,16,16,20,10,0};xf_video_rect rect={0,0,32,32};
    memset(y,111,sizeof(y));memset(cb,11,sizeof(cb));memset(cr,222,sizeof(cr));
    for(row=0;row<16;++row) for(x=0;x<16;++x)y[row*20+x]=yuv[x/2][0];
    for(row=0;row<8;++row) for(x=0;x<8;++x) {cb[row*10+x]=yuv[x][1];cr[row*10+x]=yuv[x][2];}
    xf_video_pixels(tiled,32,32,&image,&rect);
    for(i=0;i<8;++i) for(row=0;row<32;++row) {
        uint32_t p=tiled[xf_video_tile(i*4+1,row,32)];
        /* Emulate a big-endian PowerPC store explicitly, even on x86 hosts. */
        unsigned char bytes[4]={(unsigned char)(p>>24),(unsigned char)(p>>16),(unsigned char)(p>>8),(unsigned char)p};
        CHECK(abs((int)bytes[0]-rgb[i][2])<=2 && abs((int)bytes[1]-rgb[i][1])<=2 && abs((int)bytes[2]-rgb[i][0])<=2 && bytes[3]==0);
    }
    CHECK(xf_video_bgra(0x12,0x34,0x56)==0x56341200U);
    { xf_video_info info={320,240,30,1};xf_video_rect r=xf_video_fit(&info,640,384);CHECK(r.x==64 && !r.y && r.width==512 && r.height==384);
      info.width=640;info.height=360;r=xf_video_fit(&info,640,384);CHECK(!r.x && r.y==12 && r.height==360);
      info.pixel_aspect=2;r=xf_video_fit(&info,640,384);CHECK(r.width==640 && r.height==180); }
}
int main(void) {
    char filename[]="/tmp/xenonfin-video-tests-XXXXXX";int fd=mkstemp(filename);FILE *file;unsigned i,before;uint64_t frozen,finish;
    CHECK(fd>=0);file=fdopen(fd,"wb");CHECK(file!=NULL);for(i=0;i<200;++i)fputc(0,file);fclose(file);
    xf_video_init(&player,&codec);reset(100);CHECK(xf_video_start(&player,filename,&backend));
    for(i=0;i<1000 && player.state!=XF_VIDEO_ENDED;++i) {before=calls;step(10000);CHECK(calls-before<=1 && player.count<=XF_VIDEO_QUEUE);}
    CHECK(player.state==XF_VIDEO_ENDED && player.decoded==100 && player.rendered==100 && !player.dropped);
    CHECK(player.max_occupancy==XF_VIDEO_QUEUE && player.decode_us==60000 && player.max_decode_us==600);
    CHECK(!player.bytes && !player.file && player.backend_started);xf_video_stop(&player);CHECK(ends==1);
    reset(100);CHECK(xf_video_start(&player,filename,&backend));
    for(i=0;i<30;++i) step(10000);
    xf_video_pause(&player,(uint32_t)(micro/1000));frozen=player.timeline_us;before=calls;
    for(i=0;i<200;++i)step(10000);
    CHECK(player.timeline_us==frozen && calls==before && player.fps_milli==0);
    xf_video_pause(&player,(uint32_t)(micro/1000));micro+=10000;tick();CHECK(player.timeline_us>=frozen+10000 && player.timeline_us<frozen+12000);
    micro+=1000000;before=calls;tick();CHECK(calls-before<=1 && player.dropped>0);
    reset(10);micro=0xffffff00ULL*1000;CHECK(xf_video_start(&player,filename,&backend));
    for(i=0;i<100 && player.state!=XF_VIDEO_ENDED;++i)step(10000);
    CHECK(player.state==XF_VIDEO_ENDED && player.rendered==10 && !player.dropped); /* ms rollover */
    reset(20);CHECK(xf_video_start(&player,filename,&backend));tick();blocked=1;
    for(i=0;i<100;++i)step(10000);
    CHECK(player.pending && player.count==XF_VIDEO_QUEUE && calls==XF_VIDEO_QUEUE+1 && !player.clock_started);
    xf_video_stop(&player);CHECK(!player.bytes && !player.pending && !player.count && ends==1);
    for(i=1;i<=5;++i) {
        reset(10);mode=i;CHECK(xf_video_start(&player,filename,&backend));
        for(before=0;before<100 && player.state!=XF_VIDEO_ERROR;++before)step(10000);
        CHECK(player.state==XF_VIDEO_ERROR && !player.bytes && !player.file && !player.backend_started && strstr(player.message,"fault"));
    }
    /* Reproduce hardware-scale decode/prepare costs, async completion and a
     * 20 ms UI update at 4 Hz. Verify speed and no gratuitous frame drops. */
    for(i=0;i<3;++i) {
        unsigned loops=0;uint64_t ui_at=250000;
        reset(240);fps=i==2?30000.0/1001:30;decode_cost=3300;prepare_cost=i?1000:12000;gpu_cost=i?6000:0;copy_cost=2000;
        CHECK(xf_video_start(&player,filename,&backend));
        while(player.state!=XF_VIDEO_ENDED && loops++<10000) {
            unsigned wait;
            tick();CHECK(player.count<=XF_VIDEO_QUEUE);
            if(micro>=ui_at) {micro+=20000;ui_at+=250000;}
            wait=xf_video_wait_us(&player);CHECK(wait>=1000 && wait<=(player.state==XF_VIDEO_PLAYING?4000U:10000U));micro+=wait;
        }
        finish=micro;
        CHECK(player.state==XF_VIDEO_ENDED && player.decoded==240 && player.rendered==240 && !player.dropped);
        CHECK(finish-first_display>=(uint64_t)(239*1000000.0/fps) && finish-first_display<(uint64_t)(239*1000000.0/fps)+60000);
    }
    reset(1);CHECK(xf_video_start_colours(&player,&backend));
    tick();CHECK(player.synthetic && player.state==XF_VIDEO_ENDED && player.rendered==1);
    reset(10);CHECK(xf_video_start(&player,filename,&backend));xf_video_stop(&player);CHECK(!player.bytes && !begins);
    CHECK(!xf_video_start(&player,"/does-not-exist/video.m1v",&backend));CHECK(player.state==XF_VIDEO_ERROR);
    file=fopen(filename,"wb");CHECK(file!=NULL);fputc(0,file);fclose(file);CHECK(!xf_video_start(&player,filename,&backend));
    file=fopen(filename,"wb");CHECK(file!=NULL);CHECK(!fseek(file,XF_VIDEO_MAX_FILE,SEEK_SET));fputc(0,file);fclose(file);
    CHECK(!xf_video_start(&player,filename,&backend));unlink(filename);pixels_test();
    printf("Video prepared-frame PTS scheduler, hardware-cost model, pause/errors, BE colour vectors: %u assertions passed\n",checks);return 0;
}
