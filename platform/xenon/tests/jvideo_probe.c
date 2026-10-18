#define _POSIX_C_SOURCE 200809L
#include "posix_tcp.h"
#include "../audio_native.h"
#include "../jvideo.h"
#include "../video_pixels.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static xf_jvideo movie;static xf_video player;static xf_http_client api;
static uint64_t base_us,scale=1,checksums,first_hash;static unsigned images,changes;
static uint32_t pixels[640*384];
static uint64_t hw_at,hw_fraction,hw_consumed,energy,max_av_late;
static unsigned hw_bytes;static FILE *pcm_file;
static int console_cost,force_recovery,recovery_injected;static uint64_t gpu_at;
static int lookahead_test,lookahead_released;static uint64_t lookahead_at;
static int sync_test,pending_flash,last_flash,stall_test,allow_tail;
static uint64_t sync_max_error;
static unsigned short_stall;static long interruption_ns=250000000;
static unsigned hw_rate=48000;static uint64_t pause_duration=700000,stall_at;
static uint64_t beep_frames[16],flash_audio_us[16];static unsigned beeps,flashes;
/* Host DMA model consumes real decoded PCM at its own sample clock. Only the
 * final output device is simulated; HTTP, PS, decoding and scheduling are C. */
static void hw_tick(void);
static int audio_queued(void *p){(void)p;hw_tick();return (int)hw_bytes;}
static int audio_free(void *p){(void)p;hw_tick();return 65536-(int)hw_bytes;}
static int audio_submit(void *p,const unsigned char *bytes,size_t n);

static uint64_t wall(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static uint64_t clock_us(void *p) {(void)p;return (wall()-base_us)*scale;}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"jvideo line %d: %s; movie=%d %s; player=%d %s; ps=%s es=%s\n",__LINE__,#x,movie.state,movie.message,player.state,player.message,movie.ps.error,xf_mpeg1_stream_error());exit(1);}}while(0)
static void hw_tick(void) {
    uint64_t now=wall(),frames;
    if(!hw_at){hw_at=now;return;}
    hw_fraction+=(now-hw_at)*hw_rate;hw_at=now;frames=hw_fraction/1000000;hw_fraction%=1000000;
    if(frames>hw_bytes/4)frames=hw_bytes/4;
    hw_bytes-=(unsigned)frames*4;hw_consumed+=frames;
}
static int audio_submit(void *p,const unsigned char *bytes,size_t n) {
    size_t i;(void)p;hw_tick();CHECK(n==2048 && hw_bytes+n<=XF_AV_DMA_BYTES);
    for(i=0;i<n;i+=2){int sample=(int16_t)(bytes[i]|((unsigned)bytes[i+1]<<8));energy+=(uint64_t)((int64_t)sample*sample);}
    if(sync_test)for(i=0;i<n;i+=4) {
        int sample=(int16_t)(bytes[i]|((unsigned)bytes[i+1]<<8));
        uint64_t at=movie.audio.submitted+i/4;
        if(abs(sample)>4000 && (!beeps || at-beep_frames[beeps-1]>24000)) {CHECK(beeps<16);beep_frames[beeps++]=at;}
    }
    hw_bytes+=(unsigned)n;
    if(pcm_file)CHECK(fwrite(bytes,1,n,pcm_file)==n);
return 1;}
void xenon_sound_init(void){}
int xenon_sound_get_unplayed(void){return audio_queued(NULL);}
int xenon_sound_get_free(void){return audio_free(NULL);}
void xenon_sound_submit(void *bytes,int size){CHECK(audio_submit(NULL,bytes,(size_t)size));}
unsigned xf_test_audio_register(unsigned offset){
    uint64_t total;unsigned last;
    if(offset==0)return 0xa0000;
    if(offset==8)return 0x1d08001c;
    hw_tick();total=hw_consumed*4+hw_bytes;last=(unsigned)((total/2048-1)&31);
    if(!total)return 0x8000;
    return !hw_bytes?0x8000|(last<<8)|last:0x8000|((2048-(unsigned)((hw_consumed*4)%2048))<<16)|(last<<8)|(unsigned)((hw_consumed*4/2048)&31);
}
uint32_t xf_test_audio_descriptor(unsigned base,unsigned i){return __builtin_bswap32(i&1?0x80000800U:base+256+i/2*2048);}
static int begin(void *p,const xf_video_info *info,char *e,size_t n) {(void)p;(void)e;(void)n;CHECK(info->width<=720 && info->height<=480 && info->fps<=30);return 1;}
static int submit(void *p,const xf_video_image *frame,char *e,size_t n) {
    xf_video_info info={frame->width,frame->height,30,1};xf_video_rect rect=xf_video_fit(&info,640,384);unsigned i;uint64_t hash=1469598103934665603ULL;
    (void)p;(void)e;(void)n;xf_video_pixels(pixels,640,384,frame,&rect);
    for(i=0;i<640*384;i+=97)hash=(hash^pixels[i])*1099511628211ULL;
    if(!images)first_hash=hash;else if(hash!=first_hash)++changes;
    pending_flash=frame->y[(frame->height/2)*frame->y_stride+frame->width/2]>200;
    checksums^=hash;gpu_at=wall()+(console_cost?1400:0);return 1;
}
static int poll(void *p,char *e,size_t n){(void)p;(void)e;(void)n;return wall()>=gpu_at;}
static int present(void *p,char *e,size_t n){
    uint64_t us;(void)p;(void)e;(void)n;
    if(movie.audio.enabled){CHECK(xf_av_audio_clock(&movie.audio,&us));CHECK(player.pending_pts<=us);if(us-player.pending_pts>max_av_late)max_av_late=us-player.pending_pts;
        if(sync_test && pending_flash && !last_flash){CHECK(flashes<16);flash_audio_us[flashes++]=us;}
        last_flash=pending_flash;
    }
    if(console_cost){struct timespec t={0,2880000};nanosleep(&t,NULL);}
    ++images;return 1;
}
static void end(void *p){(void)p;}
static int cost_next(xf_video_image *image,char *error,size_t n) {
    int rc=xf_mpeg1_codec.next(image,error,n);
    if(console_cost && rc==1){struct timespec t={0,11880000};nanosleep(&t,NULL);}
    return rc;
}
static const xf_video_codec costly_codec={"MPEG-1 + injected console costs",NULL,cost_next,NULL};
static uint32_t step(void) {
    uint32_t now=(uint32_t)((wall()-base_us)/1000);struct timespec delay={0,500000};
    uint64_t start=wall();
    /* Interrupt as soon as output starts: the compressed-video backlog can
     * otherwise receive this entire short fixture before the old 3 s trigger. */
    if(stall_test && !stall_at && movie.audio.started) {
        CHECK(movie.stream.active && !movie.stream.done);
        stall_at=wall();
    }
    xf_audio_native_service(clock_us(NULL));
    xf_jvideo_service(&movie,now);
    if(!stall_at || wall()-stall_at>5000000)xf_test_tcp_poll();
    movie.network_us+=wall()-start;
    xf_jvideo_tick(&movie,now,1);
    if(lookahead_test && !lookahead_released && player.state==XF_VIDEO_PLAYING) {
        if(!lookahead_at)lookahead_at=wall();
        CHECK(wall()-lookahead_at<3000000 && !hw_consumed && !movie.audio.submitted);
        if(movie.ps.ahead_peak>=65536)lookahead_released=1;
    } else if(movie.mode!=XF_AV_AUDIO_ONLY || !xf_jvideo_active(&movie))xf_video_tick(&player,now);
    xf_jvideo_service(&movie,now);xf_video_service(&player);xf_jvideo_service(&movie,now);
    if(force_recovery && !recovery_injected && player.timeline_us>3000000 && player.pending && player.ready && player.pending_pts>player.timeline_us) {
        /* Pause CPU service long enough for real model DMA to drain. No fake
         * sample advancement: hw_tick accounts only actual queued frames. */
        struct timespec interruption={0,interruption_ns};nanosleep(&interruption,NULL);hw_tick();
        recovery_injected=1;
    }
    nanosleep(&delay,NULL);return now;
}
int main(int argc,char **argv) {
    xf_endpoint ep;char error[180],path[300];const char *token=getenv("JELLYFIN_ACCESS_TOKEN"),*user=getenv("JELLYFIN_USER_ID");xf_item item;uint32_t now=0;uint64_t deadline;int paused=0,resumed=0,stop_early=0,held_pause=0;uint64_t pause_at=0,position=0;unsigned shown=0;
    xf_audio_backend audio;
    xf_video_backend backend={begin,submit,poll,end,clock_us,"host production CPU pixel converter",NULL,present,NULL,NULL};
    CHECK(argc>=3 && token && user && xf_endpoint_parse(argv[1],&ep,error,sizeof(error)));
    if(argc>3 && !strcmp(argv[3],"--accelerated"))scale=20;
    if(argc>3 && !strcmp(argv[3],"--stop"))stop_early=1;
    if(argc>3 && !strcmp(argv[3],"--sync"))sync_test=1;
    if(argc>3 && !strcmp(argv[3],"--lookahead"))lookahead_test=1;
    if(argc>3 && !strcmp(argv[3],"--pause-long"))pause_duration=20000000;
    if(argc>3 && !strcmp(argv[3],"--stall"))stall_test=1;
    if(argc>3 && !strcmp(argv[3],"--slow-audio"))hw_rate=46000;
    if(argc>3 && !strcmp(argv[3],"--console-cost"))console_cost=1;
    if(argc>3 && !strcmp(argv[3],"--recovery")){console_cost=1;force_recovery=1;}
    if(argc>3 && !strcmp(argv[3],"--short-stall")){console_cost=1;force_recovery=1;short_stall=1;interruption_ns=60000000;}
    if(argc>3 && (!strcmp(argv[3],"--audio-tail") || !strcmp(argv[3],"--audio-gap")))allow_tail=1;
    xf_audio_native_init(&audio);xf_audio_native_bind(&audio,XF_AUDIO_MOVIE);memset(&item,0,sizeof(item));xf_video_init(&player,&xf_mpeg1_codec);xf_jvideo_init(&movie,&player);xf_jvideo_audio(&movie,&audio);xf_http_init(&api);xf_test_tcp_init(&ep);base_us=wall();
    if(console_cost){static xf_video_codec codec;codec=costly_codec;codec.open=xf_mpeg1_codec.open;codec.close=xf_mpeg1_codec.close;player.codec=&codec;}
    if(argc>3 && !strcmp(argv[3],"--mode-a"))CHECK(xf_jvideo_mode(&movie,XF_AV_VIDEO_ONLY));
    if(argc>3 && !strcmp(argv[3],"--mode-b"))CHECK(xf_jvideo_mode(&movie,XF_AV_AUDIO_ONLY));
    snprintf(path,sizeof(path),"/Users/%s/Items/%s",user,argv[2]);xf_http_get(&api,&ep,"native-video-test",token,path,0);
    deadline=wall()+25000000;
    while(api.active && wall()<deadline) {now=step();xf_http_tick(&api,now,1);}
    CHECK(api.done && !api.error[0] && xf_json_string(api.response.body,api.response.body_size,"Id",item.id,sizeof(item.id))==1 && xf_json_string(api.response.body,api.response.body_size,"Name",item.name,sizeof(item.name))==1 && xf_json_string(api.response.body,api.response.body_size,"Type",item.type,sizeof(item.type))==1);
    CHECK(!strcmp(item.type,"Movie") || !strcmp(item.type,"Episode"));
    CHECK(xf_jvideo_play(&movie,&ep,"native-video-test",token,user,&item,&backend,now));
    if(getenv("XENONFIN_MOVIE_PCM")){pcm_file=fopen(getenv("XENONFIN_MOVIE_PCM"),"wb");CHECK(pcm_file);}
    deadline=wall()+900000000;
    while(xf_jvideo_active(&movie) && wall()<deadline) {
        now=step();CHECK(movie.state!=XF_JV_ERROR && player.state!=XF_VIDEO_ERROR);
        if(!paused && player.timeline_us>=2000000) {xf_jvideo_pause(&movie,now);paused=1;}
        if(paused && !held_pause && player.paused && !hw_bytes){held_pause=1;pause_at=wall();position=player.timeline_us;shown=player.rendered;}
        if(held_pause && !resumed) {
            CHECK(player.timeline_us==position && player.rendered==shown && !hw_bytes);
            if(wall()-pause_at>pause_duration) {xf_jvideo_pause(&movie,now);resumed=1;}
        }
        if(stop_early && resumed && player.timeline_us>4000000) {xf_jvideo_stop(&movie,now);stop_early=0;}
    }
    CHECK(movie.state==XF_JV_FINISHED && movie.sent_start && movie.sent_stop && movie.cleaned && movie.report_status==204 && !movie.report_errors);
    CHECK(movie.start_status==204 && movie.stop_status==204 && movie.cleanup_status==204);
    if(player.timeline_us>6000000 && scale==1)CHECK(movie.progress_reports && movie.progress_status==204);
    CHECK(movie.ps.received>100000 && movie.stream_status==200 && !movie.token[0]);
    if(movie.mode==XF_AV_AUDIO_ONLY)CHECK(!images && !player.decoded && movie.audio.decoder.mp3_frames);
    else CHECK(images==player.rendered && images>30 && changes>20 && player.decoded>30);
    CHECK(movie.ps.peak<=XF_PS_RING && paused && resumed);
    if(lookahead_test)CHECK(lookahead_released && movie.ps.ahead_peak>=65536 && movie.ps.ahead_peak<=movie.ps.ahead_capacity && !movie.audio.bridge_frames && !movie.audio.overlap_seen);
    if(movie.audio.enabled)CHECK(movie.audio.decoder.mp3_frames>0 && movie.audio.decoder.rate==48000 && energy>100000000 && !hw_bytes && max_av_late<100000 && !movie.audio.failed);
    if(movie.audio.enabled) {
        CHECK(movie.audio.played==movie.audio.submitted && hw_consumed==movie.audio.played);
        if(!allow_tail)CHECK(!movie.audio.bridge_frames && !movie.audio.late_frames && !movie.audio.gap_events);
        if(player.state==XF_VIDEO_ENDED)CHECK(movie.audio.decoder.output_frames==movie.audio.pcm_frames+movie.audio.trimmed_frames);
    }
    if(force_recovery)CHECK(recovery_injected && (short_stall?!movie.audio.underruns:movie.audio.underruns>0));
    if(stall_test)CHECK(stall_at && movie.audio.underruns+player.underruns>0 && movie.rebuffer_count && movie.rebuffers[(movie.rebuffer_count-1)%XF_AV_REBUFFERS].reason[0]);
    if(pcm_file)fclose(pcm_file);
    if(sync_test) {
        unsigned i;CHECK(beeps==8 && flashes==8);
        for(i=0;i<beeps;++i){int64_t error=(int64_t)flash_audio_us[i]-((int64_t)(beep_frames[i]*1000000/48000)+movie.audio.clock_offset_us);CHECK(error> -50000 && error<50000);if((uint64_t)llabs(error)>sync_max_error)sync_max_error=(uint64_t)llabs(error);}
    }
    CHECK(!movie.audio.enabled || (movie.pts_count[0] && movie.pts_count[1]));
    fprintf(stderr,"MP3 frames=%u Hz=%u channels=%u actual-consumed=%llu max-AV-late-us=%llu initial-PTS-offset-us=%lld audio-underruns=%u PTS-error-us=%lld\n",movie.audio.decoder.mp3_frames,movie.audio.decoder.rate,movie.audio.decoder.channels,(unsigned long long)hw_consumed,(unsigned long long)max_av_late,(long long)movie.audio.initial_offset_us,movie.audio.underruns,(long long)movie.audio.timestamp_error_us);
    printf("{\"lookahead_peak\":%u,\"overlap_seen\":%u,",(unsigned)movie.ps.ahead_peak,movie.audio.overlap_seen);
    printf("\"decoded\":%u,\"presented\":%u,\"dropped\":%u,\"underruns\":%u,\"received\":%llu,\"ps_peak\":%u,\"moving_rgb_frames\":%u,\"source_runtime_ticks\":%llu,\"position_us\":%llu,\"reports_204\":true,\"cleaned\":true,\"accelerated_host_clock\":%llu,\"backpressure\":%u,\"audio_frames\":%u,\"audio_hz\":%u,\"audio_channels\":%u,\"audio_consumed_frames\":%llu,\"audio_underruns\":%u,\"av_max_late_us\":%llu,\"audio_initial_offset_us\":%lld,\"audio_pts_error_us\":%lld,\"sync_flashes\":%u,\"sync_beeps\":%u,\"audio_expected\":%u,\"device_rate_test\":%u,\"bridge_frames\":%llu,\"late_audio_trim_frames\":%llu,\"gap_events\":%u,\"flash_beep_max_error_us\":%llu,\"mp3_max_us\":%llu,\"video_decode_max_us\":%llu,\"refill_max_us\":%llu,\"refill_gap_max_us\":%llu,\"rebuffer_events\":%u}\n",player.decoded,player.rendered,player.dropped,player.underruns,(unsigned long long)movie.ps.received,(unsigned)movie.ps.peak,changes,(unsigned long long)movie.source.runtime,(unsigned long long)player.timeline_us,(unsigned long long)scale,xf_test_tcp_backpressure(),movie.audio.decoder.mp3_frames,movie.audio.decoder.rate,movie.audio.decoder.channels,(unsigned long long)hw_consumed,movie.audio.underruns,(unsigned long long)max_av_late,(long long)movie.audio.initial_offset_us,(long long)movie.audio.timestamp_error_us,flashes,beeps,movie.audio.enabled,hw_rate,(unsigned long long)movie.audio.bridge_frames,(unsigned long long)movie.audio.late_frames,movie.audio.gap_events,(unsigned long long)sync_max_error,(unsigned long long)movie.audio_times.max,(unsigned long long)player.max_decode_us,(unsigned long long)movie.refill_times.max,(unsigned long long)movie.max_service_gap_us,movie.rebuffer_count);
    {const xf_audio_native_metrics *d=xf_audio_native_status();CHECK(!d->failed&&!d->overruns&&!d->format_errors&&!d->owner&&!d->ownership_conflicts&&!d->shutdown_errors&&d->submitted==d->consumed);}
    xf_jvideo_forget(&movie);xf_video_stop(&player);xf_test_tcp_finish();return 0;
}
