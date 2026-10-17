#define _POSIX_C_SOURCE 200809L
#include "posix_tcp.h"
#include "../audio_native.h"
#include "../music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"native music integration line %d: %s; status %s; %s; %s\n",__LINE__,#x,xf_music_status(&music),music.message,music.report_message);exit(1);} } while(0)
static xf_music music;
static xf_http_client api;
static xf_browser browser;
static xf_endpoint endpoint;
static const char *token,*user;
static uint64_t wall_start, audio_total, energy;
static unsigned hw_bytes, submissions;
static uint32_t last_tick;
static uint64_t wall_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000+(unsigned)t.tv_nsec/1000000;}
static int available(void *p){(void)p;return 65536-(int)hw_bytes;}
static int queued(void *p){(void)p;return(int)hw_bytes;}
static int submit(void *p,const unsigned char *bytes,size_t size){
    size_t i;(void)p;CHECK(size%2048==0&&hw_bytes+size<=32768);hw_bytes+=(unsigned)size;audio_total+=size/4;++submissions;
    for(i=0;i<size;i+=2){int16_t sample=(int16_t)((unsigned)bytes[i]|((unsigned)bytes[i+1]<<8));energy+=(uint64_t)((int64_t)sample*sample);}
return 1;}
void xenon_sound_init(void){}
int xenon_sound_get_unplayed(void){return queued(NULL);}
int xenon_sound_get_free(void){return available(NULL);}
void xenon_sound_submit(void *bytes,int size){CHECK(submit(NULL,bytes,(size_t)size));}
unsigned xf_test_audio_register(unsigned offset){
    uint64_t total=audio_total*4;unsigned last=(unsigned)((total/2048-1)&31);
    if(offset==0)return 0xa0000;
    if(offset==8)return 0x1d08001c;
    if(!total)return 0x8000;
    return !hw_bytes?0x8000|(last<<8)|last:0x8000|((2048-(unsigned)((total-hw_bytes)%2048))<<16)|(last<<8)|(unsigned)(((total-hw_bytes)/2048)&31);
}
uint32_t xf_test_audio_descriptor(unsigned base,unsigned i){return __builtin_bswap32(i&1?0x80000800U:base+256+i/2*2048);}
static uint32_t step(void){
    struct timespec delay={0,2000000};uint32_t now=(uint32_t)(wall_ms()-wall_start),elapsed=now-last_tick;
    unsigned drained=elapsed>1000?192000:elapsed*192;
    if(drained>hw_bytes)drained=hw_bytes;
    hw_bytes-=drained;last_tick=now;xf_audio_native_service((uint64_t)now*1000);xf_test_tcp_poll();xf_music_tick(&music,now,1);nanosleep(&delay,NULL);return now;
}
static void wait_ms(unsigned duration){uint64_t end=wall_ms()+duration;while(wall_ms()<end){step();CHECK(!music.session_expired&&music.state!=XF_MUSIC_ERROR);}}
static void request(enum xf_browse_action action){
    char path[1501];uint32_t now=(uint32_t)(wall_ms()-wall_start);uint64_t deadline=wall_ms()+22000;
    CHECK(xf_browser_request(&browser,action,user,path,sizeof(path))==1);
    xf_http_get(&api,&endpoint,"native-music-test",token,path,now);
    while(api.active&&wall_ms()<deadline){now=step();xf_http_tick(&api,now,1);}
    CHECK(api.done&&!api.active&&!api.error[0]&&xf_browser_result(&browser,&api.response,"")==1);
    xf_wipe(&api.response,sizeof(api.response));
}
static void wait_playing(const char *id, unsigned rate){
    uint64_t deadline=wall_ms()+25000;
    while(wall_ms()<deadline&&(!music.started||strcmp(music.track.id,id)||music.state!=XF_MUSIC_PLAYING||music.decoder.rate!=rate)){
        step();CHECK(!music.session_expired&&music.state!=XF_MUSIC_ERROR);
    }
    CHECK(music.started&&!strcmp(music.track.id,id)&&music.decoder.rate==rate&&music.state==XF_MUSIC_PLAYING);
}
static void stop(void){
    uint64_t deadline=wall_ms()+22000;xf_music_stop(&music,(uint32_t)(wall_ms()-wall_start));
    while(xf_music_active(&music)&&wall_ms()<deadline)step();
    CHECK(music.state==XF_MUSIC_FINISHED&&!hw_bytes&&music.sent_stop&&music.report_status==204&&!music.report_message[0]);
}
int main(int argc,char **argv){
    xf_audio_backend backend;char error[128],ids[3][65];unsigned i;uint64_t position,pause_position;
    token=getenv("JELLYFIN_ACCESS_TOKEN");user=getenv("JELLYFIN_USER_ID");
    CHECK(argc==2&&token&&user&&xf_endpoint_parse(argv[1],&endpoint,error,sizeof(error)));
    xf_audio_native_init(&backend);xf_audio_native_bind(&backend,XF_AUDIO_MUSIC);xf_music_init(&music,&backend);xf_http_init(&api);xf_browser_init(&browser);xf_test_tcp_init(&endpoint);wall_start=wall_ms();
    request(XF_ROOT);for(i=0;i<browser.page.count;++i)if(!strcmp(browser.page.items[i].collection,"music"))break;
    CHECK(i<browser.page.count);browser.current.selected=i;request(XF_OPEN);CHECK(browser.page.count&& !strcmp(browser.page.items[0].type,"MusicArtist"));
    request(XF_OPEN);CHECK(browser.page.count&&!strcmp(browser.page.items[0].type,"MusicAlbum"));request(XF_OPEN);CHECK(browser.page.count==3);
    for(i=0;i<3;++i){CHECK(!strcmp(browser.page.items[i].type,"Audio")&&browser.page.items[i].index==i+1&&browser.page.items[i].artist[0]&&browser.page.items[i].album[0]);strcpy(ids[i],browser.page.items[i].id);}
    CHECK(xf_music_play(&music,&endpoint,"native-music-test",token,user,&browser.page.items[0],&browser.current,0,3,0));
    wait_playing(ids[0],44100);CHECK(!music.source.transcode&&music.decoder.channels==1);
    while(xf_music_position(&music)<52000000){step();CHECK(music.state!=XF_MUSIC_ERROR);}
    CHECK(music.sent_start&&music.report_status==204&&energy>100000000);
    xf_music_pause(&music);wait_ms(300);pause_position=xf_music_position(&music);position=music.submitted;wait_ms(700);
    CHECK(xf_music_position(&music)==pause_position&&music.submitted==position&&!hw_bytes);
    xf_music_pause(&music);wait_ms(500);CHECK(xf_music_position(&music)>pause_position+2000000);
    xf_music_skip(&music,1,(uint32_t)(wall_ms()-wall_start));wait_playing(ids[1],48000);CHECK(music.decoder.channels==2&&!music.source.transcode);
    wait_ms(300);xf_music_skip(&music,-1,(uint32_t)(wall_ms()-wall_start));wait_playing(ids[0],44100);wait_ms(300);stop();
    /* Let stereo MP3 reach EOF, including its partial DMA tail, then advance
     * through the real album query into a server-transcoded FLAC track. */
    CHECK(xf_music_play(&music,&endpoint,"native-music-test",token,user,&browser.page.items[1],&browser.current,1,3,(uint32_t)(wall_ms()-wall_start)));
    wait_playing(ids[1],48000);
    wait_playing(ids[2],44100);CHECK(music.source.transcode);wait_ms(700);stop();CHECK(music.cleaned);
    CHECK(xf_music_play(&music,&endpoint,"native-music-test",token,user,&browser.page.items[2],&browser.current,2,3,(uint32_t)(wall_ms()-wall_start)));
    wait_playing(ids[2],44100);
    {
        uint64_t deadline=wall_ms()+10000;
        while(xf_music_active(&music)&&wall_ms()<deadline)step();
        CHECK(music.state==XF_MUSIC_FINISHED&&music.sent_stop&&music.cleaned&&!hw_bytes&&music.report_status==204);
    }
    CHECK(xf_test_tcp_backpressure()>0);CHECK(energy>100000000&&audio_total>48000*6);
    printf("{\"native_mp3_playback\":true,\"mono_44100\":true,\"stereo_48000\":true,\"flac_to_mp3\":true,\"pause_resume\":true,\"skip_previous\":true,\"natural_end_and_advance\":true,\"reports_204\":true,\"pcm_frames_submitted\":%llu,\"pcm_energy\":%llu,\"backpressure_retries\":%u,\"submissions\":%u}\n",(unsigned long long)audio_total,(unsigned long long)energy,xf_test_tcp_backpressure(),submissions);
    {const xf_audio_native_metrics *d=xf_audio_native_status();CHECK(!d->failed&&!d->overruns&&!d->format_errors&&!d->owner&&!d->ownership_conflicts&&!d->shutdown_errors&&d->submitted==d->consumed);}
    xf_music_forget(&music);CHECK(!music.token[0]&&!music.track.id[0]&&!music.decoder.pcm_size);xf_test_tcp_finish();return 0;
}
