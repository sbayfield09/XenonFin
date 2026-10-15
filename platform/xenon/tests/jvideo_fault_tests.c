#include "../jvideo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"jvideo fault line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#include "tcp_fake.h"
static xf_video player;static xf_jvideo movie;static uint64_t micro;
static const char negotiation[]="{\"PlaySessionId\":\"session\",\"MediaSources\":[{\"Id\":\"source\",\"Protocol\":\"File\",\"Container\":\"mp4\",\"SupportsTranscoding\":true,\"TranscodingSubProtocol\":\"http\",\"TranscodingContainer\":\"mpeg\",\"MediaStreams\":[{\"Type\":\"Video\",\"Codec\":\"h264\",\"Width\":1920,\"Height\":1080,\"Index\":0}]}]}";
static int begin(void *p,const xf_video_info *i,char *e,size_t n){(void)p;(void)i;(void)e;(void)n;CHECK(0);return 0;}
static int submit(void *p,const xf_video_image *i,char *e,size_t n){(void)p;(void)i;(void)e;(void)n;CHECK(0);return 0;}
static int poll(void *p,char *e,size_t n){(void)p;(void)e;(void)n;return 1;}
static int present(void *p,char *e,size_t n){(void)p;(void)e;(void)n;CHECK(0);return 0;}
static void end(void *p){(void)p;}
static uint64_t clock_us(void *p){(void)p;return micro;}
static void start(void){
    xf_endpoint ep;xf_item item;char error[128];xf_video_backend backend={begin,submit,poll,end,clock_us,"fault-test",NULL,present,NULL,NULL};
    xf_video_init(&player,&xf_mpeg1_codec);xf_jvideo_init(&movie,&player);memset(&item,0,sizeof(item));strcpy(item.id,"movie");strcpy(item.type,"Movie");strcpy(item.name,"Fault test");
    CHECK(xf_endpoint_parse("127.0.0.1:8096",&ep,error,sizeof(error)));CHECK(xf_jvideo_play(&movie,&ep,"device","fault-token","user",&item,&backend,0));
}
static void inject(xf_http_client *c,int status,const char *type,const void *body,size_t size,int complete){
    char h[256];size_t a;struct pbuf *p=calloc(1,sizeof(*p));
    CHECK(c->active && c->pcb);CHECK(c->pcb->connected(c->pcb->callback_arg,c->pcb,ERR_OK)==ERR_OK);
    snprintf(h,sizeof(h),"HTTP/1.1 %d Test\r\nContent-Type: %s\r\nContent-Length: %u\r\n\r\n",status,type,(unsigned)size+(complete?0:10));a=strlen(h);
    p->len=p->tot_len=(u16_t)(a+size);p->payload=malloc(a+size);memcpy(p->payload,h,a);if(size)memcpy((char *)p->payload+a,body,size);
    CHECK(c->pcb->recv(c->pcb->callback_arg,c->pcb,p,ERR_OK)==ERR_OK);
}
static unsigned audio_pending;
static int queued_audio(void *p){(void)p;return (int)audio_pending;}
static int free_audio(void *p){(void)p;return 65536-(int)audio_pending;}
static void submit_audio(void *p,const unsigned char *bytes,size_t n){(void)p;(void)bytes;(void)n;CHECK(0);}
static void ready_audio(void){
    char json[1024],*at;xf_audio_backend audio={queued_audio,free_audio,submit_audio,NULL};
    xf_jvideo_audio(&movie,&audio);audio_pending=0;strcpy(json,negotiation);at=strstr(json,"}]}]}");CHECK(at);
    snprintf(at,sizeof(json)-(size_t)(at-json),"},%s]}]}","{\"Type\":\"Audio\",\"Codec\":\"aac\",\"Index\":1}");
    inject(&movie.control,200,"application/json",json,strlen(json),1);xf_jvideo_tick(&movie,1,1);
    CHECK(movie.state==XF_JV_STREAMING && movie.audio.enabled && player.master_clock && movie.stream.active);
}
static void ready(void){inject(&movie.control,200,"application/json",negotiation,strlen(negotiation),1);xf_jvideo_tick(&movie,1,1);CHECK(movie.state==XF_JV_STREAMING && player.state==XF_VIDEO_LOADING && movie.stream.active);}
static void clean(void){
    unsigned i;for(i=0;i<5 && xf_jvideo_active(&movie);++i){if(movie.control.active)inject(&movie.control,204,"application/json",NULL,0,1);xf_jvideo_tick(&movie,10+i,1);}
    CHECK(!xf_jvideo_active(&movie) && !movie.stream.active && !movie.control.active && !movie.token[0]);xf_jvideo_forget(&movie);CHECK(!player.backend_started && !movie.token[0]);
}
int main(void){
    start();inject(&movie.control,401,"application/json",NULL,0,1);xf_jvideo_tick(&movie,1,1);CHECK(movie.session_expired && movie.state==XF_JV_ERROR);clean();
    start();inject(&movie.control,200,"application/json","{}",2,1);xf_jvideo_tick(&movie,1,1);CHECK(movie.state==XF_JV_ERROR && !movie.stream.active);clean();
    start();xf_jvideo_stop(&movie,1);xf_jvideo_tick(&movie,2,1);CHECK(movie.state==XF_JV_FINISHED && !movie.control.active);clean();
    start();ready();inject(&movie.stream,403,"text/plain",NULL,0,1);xf_jvideo_tick(&movie,2,1);CHECK(movie.failed && strstr(movie.message,"403"));clean();
    start();ready();inject(&movie.stream,401,"text/plain",NULL,0,1);xf_jvideo_tick(&movie,2,1);CHECK(movie.session_expired && movie.failed);clean();
    start();ready();inject(&movie.stream,302,"text/plain",NULL,0,1);xf_jvideo_tick(&movie,2,1);CHECK(movie.failed && strstr(movie.message,"302"));clean();
    start();ready();inject(&movie.stream,200,"text/html","<html>error</html>",18,1);xf_jvideo_tick(&movie,2,1);CHECK(movie.failed && !player.decoded);clean();
    start();ready();inject(&movie.stream,200,"video/mpeg","not MPEG-PS",11,1);xf_jvideo_tick(&movie,2,1);CHECK(movie.failed && strstr(movie.message,"start code"));clean();
    start();ready();inject(&movie.stream,200,"video/mpeg",NULL,0,0);CHECK(movie.stream.pcb->recv(movie.stream.pcb->callback_arg,movie.stream.pcb,NULL,ERR_OK)==ERR_OK);xf_jvideo_tick(&movie,2,1);CHECK(movie.failed && strstr(movie.message,"closed"));clean();
    start();ready();CHECK(movie.stream.pcb->connected(movie.stream.pcb->callback_arg,movie.stream.pcb,ERR_OK)==ERR_OK);xf_jvideo_tick(&movie,2,1);xf_jvideo_tick(&movie,8002,1);CHECK(movie.failed && strstr(movie.message,"idle timeout"));clean();
    start();ready();xf_jvideo_tick(&movie,2,0);CHECK(movie.failed && strstr(movie.message,"DHCP"));clean();
    start();ready();inject(&movie.stream,200,"video/mpeg",NULL,0,1);xf_jvideo_tick(&movie,2,1);xf_video_tick(&player,2);xf_jvideo_tick(&movie,3,1);CHECK(movie.failed && strstr(movie.message,"ended before"));clean();
    /* Reject an MPEG-2 sequence extension before allocating/displaying frames. */
    {
        static const unsigned char mpeg2[]={0,0,1,0xba,0x21,0,1,0,1,0,1,0,0,0,1,0xe0,0,17,15,0,0,1,0xb3,0x14,0,0xf0,0x15,0,0,1,0xb5,0,0,0,0};
        start();ready();inject(&movie.stream,200,"video/mpeg",mpeg2,sizeof(mpeg2),1);xf_jvideo_tick(&movie,2,1);CHECK(movie.failed && !player.decoded);clean();
    }
    start();xf_jvideo_pause(&movie,0);CHECK(movie.pause_requested);xf_jvideo_stop(&movie,1);xf_jvideo_tick(&movie,2,1);CHECK(movie.state==XF_JV_FINISHED && !movie.pause_requested);clean();
    /* Reports reflect consumed DMA position, not decoded/submitted samples. */
    start();ready_audio();movie.audio.started=1;movie.audio.submitted=10000;audio_pending=4000;player.rendered=1;
    xf_jvideo_tick(&movie,2,1);
    CHECK(movie.sent_start && strstr(movie.control.request,"\"PositionTicks\":1875000"));
    xf_jvideo_stop(&movie,3);xf_jvideo_tick(&movie,4,1);CHECK(movie.state==XF_JV_STOPPING && !movie.sent_stop);
    audio_pending=0;clean();
    start();ready_audio();audio_pending=65537;xf_jvideo_tick(&movie,2,1);CHECK(movie.failed && strstr(movie.message,"DMA"));audio_pending=0;clean();
    CHECK(closes+aborts>0 && frees>0 && writes>0);printf("Native Jellyfin video negotiation/401/container/EOF/timeouts/disconnect/cancellation: %u assertions passed\n",checks);return 0;
}
