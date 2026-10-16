#include "../music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"music fault line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
#include "tcp_fake.h"
static xf_music music;
static unsigned hardware;
static const char id[]="11111111111111111111111111111111";
static const char negotiation[]="{\"PlaySessionId\":\"session\",\"MediaSources\":[{\"Id\":\"source\",\"Protocol\":\"File\",\"Container\":\"mp3\",\"SupportsDirectPlay\":true,\"MediaStreams\":[{\"Type\":\"Audio\",\"Codec\":\"mp3\",\"SampleRate\":44100,\"Channels\":1}]}]}";
static int queued(void *p){(void)p;return(int)hardware;}
static int available(void *p){(void)p;return 65536-(int)hardware;}
static int submit(void *p,const unsigned char *bytes,size_t n){(void)p;(void)bytes;(void)n;CHECK(0);return 1;}
static void start(void){
    xf_audio_backend backend={queued,available,submit,NULL};xf_endpoint ep;xf_item track;xf_location location;char error[128];
    xf_music_init(&music,&backend);memset(&track,0,sizeof(track));memset(&location,0,sizeof(location));
    strcpy(track.id,id);strcpy(track.name,"Actual decoder fault test");strcpy(track.type,"Audio");location.view=XF_CHILDREN;strcpy(location.id,id);
    CHECK(xf_endpoint_parse("127.0.0.1:8096",&ep,error,sizeof(error)));
    CHECK(xf_music_play(&music,&ep,"device","test-only-token",id,&track,&location,9,12,0));
}
static void inject(xf_http_client *client,int status,const char *type,const char *body){
    char header[256];size_t a,b=strlen(body);struct pbuf *p=calloc(1,sizeof(*p));
    CHECK(client->active&&client->pcb);CHECK(client->pcb->connected(client->pcb->callback_arg,client->pcb,ERR_OK)==ERR_OK);
    snprintf(header,sizeof(header),"HTTP/1.1 %d Test\r\nContent-Type: %s\r\nContent-Length: %u\r\n\r\n",status,type,(unsigned)b);a=strlen(header);
    p->len=p->tot_len=(u16_t)(a+b);p->payload=malloc(a+b);memcpy(p->payload,header,a);memcpy((char*)p->payload+a,body,b);
    CHECK(client->pcb->recv(client->pcb->callback_arg,client->pcb,p,ERR_OK)==ERR_OK);
}
static void ready_stream(void){inject(&music.control,200,"application/json",negotiation);xf_music_tick(&music,1,1);CHECK(music.state==XF_MUSIC_BUFFERING&&music.stream.active);}
static void forget(void){xf_music_forget(&music);CHECK(!music.token[0]&&!music.control.active&&!music.stream.active&&!music.decoder.pcm_size);}
int main(void){
    start();inject(&music.control,401,"application/json","");xf_music_tick(&music,1,1);CHECK(music.session_expired&&!music.stream.active);forget();
    start();inject(&music.control,200,"application/json","{}");xf_music_tick(&music,1,1);CHECK(music.state==XF_MUSIC_ERROR&&strstr(music.message,"MP3 source"));forget();
    start();ready_stream();inject(&music.stream,403,"text/plain","");xf_music_tick(&music,2,1);CHECK(music.state==XF_MUSIC_ERROR&&strstr(music.message,"403"));forget();
    start();ready_stream();inject(&music.stream,200,"text/html","<html>not MP3 audio</html>");xf_music_tick(&music,2,1);CHECK(music.state==XF_MUSIC_ERROR&&!music.decoder.mp3_frames);forget();
    start();ready_stream();inject(&music.stream,200,"audio/mpeg","not an mp3");xf_music_tick(&music,2,1);CHECK(music.state==XF_MUSIC_ERROR&&strstr(music.message,"no decodable"));forget();
    start();ready_stream();inject(&music.stream,302,"text/plain","");xf_music_tick(&music,2,1);CHECK(music.state==XF_MUSIC_ERROR&&!music.stream.active);forget();
    start();ready_stream();CHECK(music.stream.pcb->connected(music.stream.pcb->callback_arg,music.stream.pcb,ERR_OK)==ERR_OK);
    xf_music_tick(&music,2,1);xf_music_tick(&music,8002,1);CHECK(music.state==XF_MUSIC_ERROR&&strstr(music.message,"idle timeout"));forget();
    start();ready_stream();xf_music_tick(&music,2,0);CHECK(music.state==XF_MUSIC_ERROR&&strstr(music.message,"DHCP"));forget();
    start();xf_music_pause(&music);CHECK(music.paused);ready_stream();CHECK(music.paused&&!music.started);xf_music_stop(&music,2);xf_music_tick(&music,3,1);CHECK(music.state==XF_MUSIC_FINISHED&&!music.stream.active&&!music.control.active);forget();
    start();xf_music_stop(&music,1);xf_music_tick(&music,2,1);CHECK(music.state==XF_MUSIC_FINISHED&&!music.control.active&&!music.control.request[0]);forget();
    /* Skip across the browser's ten-item boundary without caching an album. */
    start();xf_music_skip(&music,1,1);xf_music_tick(&music,2,1);CHECK(music.control.active&&strstr(music.control.request,"StartIndex=10&Limit=1")&&!music.started);
    inject(&music.control,200,"application/json","{\"Items\":[{\"Id\":\"22222222222222222222222222222222\",\"Name\":\"Next track\",\"Type\":\"Audio\"}],\"TotalRecordCount\":12,\"StartIndex\":10}");
    xf_music_tick(&music,3,1);CHECK(music.queue_index==10&&!strcmp(music.track.id,"22222222222222222222222222222222")&&strstr(music.control.request,"/PlaybackInfo"));forget();
    start();hardware=65536;xf_music_stop(&music,1);xf_music_tick(&music,2001,1);CHECK(music.state==XF_MUSIC_ERROR&&strstr(music.message,"DMA"));hardware=0;forget();
    CHECK(closes+aborts>0&&frees>0&&writes>0);printf("Native player authentication/invalid streams/errors/cancel/paging/drain faults: %u checks passed\n",checks);return 0;
}
