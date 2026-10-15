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
static uint64_t micro;
static unsigned calls, frames, submissions, begins, ends, blocked, mode;
static uint64_t last_pts;
static int open_codec(unsigned char *data,size_t size,xf_video_info *info,char *error,size_t cap) {
    (void)data;(void)size;(void)error;(void)cap; calls=0;
    info->width=info->height=16;info->fps=25;info->pixel_aspect=1; return 1;
}
static int next_codec(xf_video_image *image,char *error,size_t cap) {
    if(calls==frames) return 0;
    if(mode==4) {snprintf(error,cap,"decoder fault");return -1;}
    memset(source,(int)(calls%200+16),sizeof(source));
    image->width=image->height=16;image->y=source;image->cb=source+320;image->cr=source+400;
    image->y_stride=20;image->c_stride=10;image->pts_us=(uint64_t)calls*40000; ++calls;micro+=600;return 1;
}
static void close_codec(void) {}
static const xf_video_codec codec={"fake control-flow codec",open_codec,next_codec,close_codec};
static int begin(void *context,const xf_video_info *info,char *error,size_t cap) {
    (void)context;CHECK(info->width==16);++begins;
    if(mode==1) {snprintf(error,cap,"begin fault");return 0;} return 1;
}
static int submit(void *context,const xf_video_image *image,char *error,size_t cap) {
    (void)context;
    if(mode==2) {snprintf(error,cap,"submit fault");return 0;}
    CHECK(image->pts_us<=player.timeline_us || !player.clock_started);
    CHECK(image->y_stride==16 && image->c_stride==8);
    CHECK(image->y[0]==image->pts_us/40000%200+16 && image->cb[0]==image->y[0] && image->cr[0]==image->y[0]);
    CHECK(!submissions || image->pts_us>last_pts);last_pts=image->pts_us;++submissions;micro+=100;return 1;
}
static int poll(void *context,char *error,size_t cap) {
    (void)context;if(mode==3) {snprintf(error,cap,"poll fault");return -1;}return !blocked;
}
static void end(void *context) {(void)context;++ends;}
static uint64_t clock_us(void *context) {(void)context;return micro;}
static xf_video_backend backend={begin,submit,poll,end,clock_us,"mock",NULL};
static void reset(unsigned count) {
    xf_video_stop(&player); xf_video_init(&player,&codec);frames=count;submissions=begins=ends=blocked=mode=0;last_pts=0;
}
int main(void) {
    char filename[]="/tmp/xenonfin-video-tests-XXXXXX";int fd=mkstemp(filename);FILE *file;unsigned i,before;uint64_t frozen;uint32_t now;
    static uint32_t pixels[32*32];unsigned char y[16*16],cb[8*8],cr[8*8];
    xf_video_info info={320,240,30,1};xf_video_rect rect;xf_video_image image={y,cb,cr,16,16,16,8,0};
    CHECK(fd>=0);file=fdopen(fd,"wb");CHECK(file!=NULL);for(i=0;i<200;++i)fputc(0,file);fclose(file);
    xf_video_init(&player,&codec); reset(100);CHECK(xf_video_start(&player,filename,&backend));
    for(now=0;now<4500 && player.state!=XF_VIDEO_ENDED;now+=10) {
        before=calls;xf_video_tick(&player,now);CHECK(calls-before<=1 && player.count<=XF_VIDEO_QUEUE);
    }
    CHECK(player.state==XF_VIDEO_ENDED && player.decoded==100 && player.rendered==100 && !player.dropped);
    CHECK(player.max_occupancy==2 && player.decode_us==60000 && player.max_decode_us==600);
    CHECK(!player.bytes && !player.file && player.backend_started);xf_video_stop(&player);CHECK(ends==1);
    reset(100);CHECK(xf_video_start(&player,filename,&backend));
    for(now=0;now<=200;now+=10)xf_video_tick(&player,now);
    xf_video_pause(&player,200);frozen=player.timeline_us;before=calls;
    for(now=210;now<=2200;now+=10)xf_video_tick(&player,now);
    CHECK(player.timeline_us==frozen && calls==before && player.fps_milli==0);
    xf_video_pause(&player,2200);xf_video_tick(&player,2210);CHECK(player.timeline_us==frozen+10000);
    before=calls;xf_video_tick(&player,3210);CHECK(calls-before<=1 && player.dropped>0 && player.count<=2);
    reset(10);CHECK(xf_video_start(&player,filename,&backend));now=0xffffff00U;
    for(i=0;i<100 && player.state!=XF_VIDEO_ENDED;++i,now+=10)xf_video_tick(&player,now);
    CHECK(player.state==XF_VIDEO_ENDED && player.rendered==10 && !player.dropped);
    reset(20);CHECK(xf_video_start(&player,filename,&backend));xf_video_tick(&player,0);blocked=1;
    for(now=10;now<1000;now+=10)xf_video_tick(&player,now);
    CHECK(player.pending && player.count==2 && calls==3); /* Bounded even when renderer stalls. */
    xf_video_stop(&player);CHECK(!player.bytes && !player.pending && !player.count && ends==1);
    for(i=1;i<=4;++i) {
        reset(10);mode=i;CHECK(xf_video_start(&player,filename,&backend));
        for(now=0;now<100 && player.state!=XF_VIDEO_ERROR;now+=10)xf_video_tick(&player,now);
        CHECK(player.state==XF_VIDEO_ERROR && !player.bytes && !player.file && !player.backend_started && strstr(player.message,"fault"));
    }
    reset(10);CHECK(xf_video_start(&player,filename,&backend));xf_video_stop(&player);CHECK(!player.bytes && !begins);
    CHECK(!xf_video_start(&player,"/does-not-exist/video.m1v",&backend));CHECK(player.state==XF_VIDEO_ERROR);
    file=fopen(filename,"wb");CHECK(file!=NULL);fputc(0,file);fclose(file);
    CHECK(!xf_video_start(&player,filename,&backend));
    file=fopen(filename,"wb");CHECK(file!=NULL);CHECK(!fseek(file,XF_VIDEO_MAX_FILE,SEEK_SET));fputc(0,file);fclose(file);
    CHECK(!xf_video_start(&player,filename,&backend));unlink(filename);
    rect=xf_video_fit(&info,640,384);CHECK(rect.x==64 && rect.y==0 && rect.width==512 && rect.height==384);
    info.width=640;info.height=360;rect=xf_video_fit(&info,640,384);CHECK(rect.x==0 && rect.y==12 && rect.width==640 && rect.height==360);
    info.width=320;info.height=240;info.pixel_aspect=2;rect=xf_video_fit(&info,640,384);CHECK(rect.width==640 && rect.height==240 && rect.y==72);
    info.width=info.height=16;info.pixel_aspect=1;rect=xf_video_fit(&info,32,32);
    memset(y,235,sizeof(y));memset(cb,128,sizeof(cb));memset(cr,128,sizeof(cr));xf_video_pixels(pixels,32,32,&image,&rect);
    CHECK(pixels[xf_video_tile(15,15,32)]==0xffffffffU);
    memset(y,16,sizeof(y));xf_video_pixels(pixels,32,32,&image,&rect);CHECK(pixels[xf_video_tile(0,0,32)]==0xff000000U);
    memset(y,81,sizeof(y));memset(cb,90,sizeof(cb));memset(cr,240,sizeof(cr));xf_video_pixels(pixels,32,32,&image,&rect);
    CHECK((pixels[xf_video_tile(31,31,32)]&0x00ff0000U)>=0x00fc0000U && (pixels[xf_video_tile(31,31,32)]&0xffffU)<0x300U);
    printf("Video queue, clock, pause/drop/stop/errors, aspect and BT.601 pixels: %u assertions passed\n",checks);return 0;
}
