#define _POSIX_C_SOURCE 200809L
#include "posix_tcp.h"
#include "../jvideo.h"
#include "../video_pixels.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static xf_jvideo movie;static xf_video player;static xf_http_client api;
static uint64_t base_us,scale=1,checksums,first_hash;static unsigned images,changes;
static uint32_t pixels[640*384];
static uint64_t wall(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static uint64_t clock_us(void *p) {(void)p;return (wall()-base_us)*scale;}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"jvideo line %d: %s; movie=%d %s; player=%d %s; ps=%s es=%s\n",__LINE__,#x,movie.state,movie.message,player.state,player.message,movie.ps.error,xf_mpeg1_stream_error());exit(1);}}while(0)
static int begin(void *p,const xf_video_info *info,char *e,size_t n) {(void)p;(void)e;(void)n;CHECK(info->width<=720 && info->height<=480 && info->fps<=30);return 1;}
static int submit(void *p,const xf_video_image *frame,char *e,size_t n) {
    xf_video_info info={frame->width,frame->height,30,1};xf_video_rect rect=xf_video_fit(&info,640,384);unsigned i;uint64_t hash=1469598103934665603ULL;
    (void)p;(void)e;(void)n;xf_video_pixels(pixels,640,384,frame,&rect);
    for(i=0;i<640*384;i+=97)hash=(hash^pixels[i])*1099511628211ULL;
    if(!images)first_hash=hash;else if(hash!=first_hash)++changes;
    checksums^=hash;return 1;
}
static int poll(void *p,char *e,size_t n){(void)p;(void)e;(void)n;return 1;}
static int present(void *p,char *e,size_t n){(void)p;(void)e;(void)n;++images;return 1;}
static void end(void *p){(void)p;}
static uint32_t step(void) {
    uint32_t now=(uint32_t)((wall()-base_us)/1000);struct timespec delay={0,500000};
    uint64_t start=wall();xf_test_tcp_poll();movie.network_us+=wall()-start;
    xf_jvideo_tick(&movie,now,1);xf_video_tick(&player,now);xf_video_service(&player);nanosleep(&delay,NULL);return now;
}
int main(int argc,char **argv) {
    xf_endpoint ep;char error[180],path[300];const char *token=getenv("JELLYFIN_ACCESS_TOKEN"),*user=getenv("JELLYFIN_USER_ID");xf_item item;uint32_t now=0;uint64_t deadline;int paused=0,resumed=0,stop_early=0;uint64_t pause_at=0,position=0;unsigned shown=0;
    xf_video_backend backend={begin,submit,poll,end,clock_us,"host production CPU pixel converter",NULL,present,NULL,NULL};
    CHECK(argc>=3 && token && user && xf_endpoint_parse(argv[1],&ep,error,sizeof(error)));
    if(argc>3 && !strcmp(argv[3],"--accelerated"))scale=20;
    if(argc>3 && !strcmp(argv[3],"--stop"))stop_early=1;
    memset(&item,0,sizeof(item));xf_video_init(&player,&xf_mpeg1_codec);xf_jvideo_init(&movie,&player);xf_http_init(&api);xf_test_tcp_init(&ep);base_us=wall();
    snprintf(path,sizeof(path),"/Users/%s/Items/%s",user,argv[2]);xf_http_get(&api,&ep,"native-video-test",token,path,0);
    deadline=wall()+25000000;
    while(api.active && wall()<deadline) {now=step();xf_http_tick(&api,now,1);}
    CHECK(api.done && !api.error[0] && xf_json_string(api.response.body,api.response.body_size,"Id",item.id,sizeof(item.id))==1 && xf_json_string(api.response.body,api.response.body_size,"Name",item.name,sizeof(item.name))==1 && xf_json_string(api.response.body,api.response.body_size,"Type",item.type,sizeof(item.type))==1);
    CHECK(!strcmp(item.type,"Movie") || !strcmp(item.type,"Episode"));
    CHECK(xf_jvideo_play(&movie,&ep,"native-video-test",token,user,&item,&backend,now));
    deadline=wall()+420000000;
    while(xf_jvideo_active(&movie) && wall()<deadline) {
        now=step();CHECK(movie.state!=XF_JV_ERROR && player.state!=XF_VIDEO_ERROR);
        if(!paused && player.timeline_us>=2000000) {xf_video_pause(&player,now);paused=1;pause_at=wall();position=player.timeline_us;shown=player.rendered;}
        if(paused && !resumed) {
            CHECK(player.timeline_us==position && player.rendered==shown);
            if(wall()-pause_at>700000) {xf_video_pause(&player,now);resumed=1;}
        }
        if(stop_early && resumed && player.timeline_us>4000000) {xf_jvideo_stop(&movie,now);stop_early=0;}
    }
    CHECK(movie.state==XF_JV_FINISHED && movie.sent_start && movie.sent_stop && movie.cleaned && movie.report_status==204 && !movie.report_errors);
    CHECK(movie.start_status==204 && movie.stop_status==204 && movie.cleanup_status==204);
    if(player.timeline_us>6000000 && scale==1)CHECK(movie.progress_reports && movie.progress_status==204);
    CHECK(images==player.rendered && images>30 && changes>20 && player.decoded>30 && movie.ps.received>100000 && movie.stream_status==200 && !movie.token[0]);
    CHECK(movie.ps.peak<=XF_PS_RING && paused && resumed);
    printf("{\"decoded\":%u,\"presented\":%u,\"dropped\":%u,\"underruns\":%u,\"received\":%llu,\"ps_peak\":%u,\"moving_rgb_frames\":%u,\"source_runtime_ticks\":%llu,\"position_us\":%llu,\"reports_204\":true,\"cleaned\":true,\"accelerated_host_clock\":%llu,\"backpressure\":%u}\n",player.decoded,player.rendered,player.dropped,player.underruns,(unsigned long long)movie.ps.received,(unsigned)movie.ps.peak,changes,(unsigned long long)movie.source.runtime,(unsigned long long)player.timeline_us,(unsigned long long)scale,xf_test_tcp_backpressure());
    xf_jvideo_forget(&movie);xf_video_stop(&player);xf_test_tcp_finish();return 0;
}
