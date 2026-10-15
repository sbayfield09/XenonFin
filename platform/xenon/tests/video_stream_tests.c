/* Protocol/framing invariants for the actual native streaming path. */
#include "../video_ps.h"
#include "../video_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;static xf_video_ps ps;static xf_response response;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"video stream line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char output[32];static size_t used,capacity;
static int sink(unsigned char c){CHECK(used<sizeof(output));output[used++]=c;return 1;}
static size_t space(void){return capacity>used?capacity-used:0;}
static int discard(void *p,unsigned char c){(void)p;(void)c;return 1;}
static const unsigned char pack[]={0,0,1,0xba,0x21,0,1,0,1,0,1,0};
static const unsigned char packets[]={
    0,0,1,0xc0,0,4,0xff,1,2,3, /* audio is skipped */
    0,0,1,0xe0,0,8,0x21,0,1,0,1,11,22,33, /* MPEG-1 PES, zero PTS */
    0,0,1,0xe0,0,11,0x80,0x80,5,0x21,0,1,0,1,44,55,66, /* MPEG-2 PES header */
    0,0,1,0xe1,0,3,77,88,99, /* another video track is skipped */
    0,0,1,0xb9};
static void ps_reset(void){xf_video_ps_init(&ps);used=0;capacity=sizeof(output);}
static void put(const unsigned char *data,size_t n){size_t i;for(i=0;i<n;++i)CHECK(xf_video_ps_byte(&ps,data[i]));}
static void framing(void){
    unsigned i,fragment;unsigned char bytes[sizeof(pack)+sizeof(packets)];
    memcpy(bytes,pack,sizeof(pack));memcpy(bytes+sizeof(pack),packets,sizeof(packets));
    for(fragment=1;fragment<=sizeof(bytes);++fragment){
        ps_reset();
        /* Force ring wrap, packet headers and payloads across every boundary. */
        ps.head=XF_PS_RING-7;
        for(i=0;i<sizeof(bytes);){size_t n=sizeof(bytes)-i;if(n>fragment)n=fragment;put(bytes+i,n);i+=(unsigned)n;CHECK(xf_video_ps_tick(&ps,sink,space,0)>=0);}
        CHECK(xf_video_ps_tick(&ps,sink,space,1)>=0 && ps.ended && ps.have_pts && ps.pts90k==0);
        CHECK(used==6 && !memcmp(output,"\x0b\x16\x21\x2c\x37\x42",6) && ps.video_bytes==6 && ps.packets==2);
    }
    ps_reset();put(pack,sizeof(pack));put(packets,sizeof(packets));capacity=0;
    CHECK(xf_video_ps_tick(&ps,sink,space,1)>=0 && !used && !ps.failed);
    capacity=sizeof(output);CHECK(xf_video_ps_tick(&ps,sink,space,1)>=0 && used==6 && ps.ended);
    /* A complete HTTP response containing a truncated PES is still an error. */
    ps_reset();put(pack,sizeof(pack));put(packets,18);CHECK(xf_video_ps_tick(&ps,sink,space,1)<0 && strstr(ps.error,"inside"));
    ps_reset();put(pack,sizeof(pack));put((const unsigned char *)"\0\0\1\xe0\0\0",6);CHECK(xf_video_ps_tick(&ps,sink,space,0)<0);
    ps_reset();put((const unsigned char *)"\0\0\1\xb3",4);CHECK(xf_video_ps_tick(&ps,sink,space,0)<0);
    ps_reset();put(pack,sizeof(pack));put(packets,sizeof(packets));put(pack,sizeof(pack));CHECK(xf_video_ps_tick(&ps,sink,space,0)<0);
    ps_reset();for(i=0;i<XF_PS_RING;++i)CHECK(xf_video_ps_byte(&ps,0));CHECK(!xf_video_ps_space(&ps) && !xf_video_ps_byte(&ps,0));
}
static const char valid[]="{\"PlaySessionId\":\"session\",\"MediaSources\":[{\"Id\":\"source\",\"Protocol\":\"File\",\"Container\":\"mov,mp4\",\"SupportsTranscoding\":true,\"TranscodingSubProtocol\":\"http\",\"TranscodingContainer\":\"mpeg\",\"RunTimeTicks\":3000000000,\"MediaStreams\":[{\"Type\":\"Video\",\"Codec\":\"h264\",\"Width\":1920,\"Height\":1080,\"Index\":0,\"VideoRangeType\":\"SDR\"},{\"Type\":\"Audio\",\"Codec\":\"aac\"}]}]}";
static int parse(const char *json,xf_video_source *s){char error[180];xf_response_init(&response);response.status=200;response.complete=1;snprintf(response.body,sizeof(response.body),"%s",json);response.body_size=strlen(json);return xf_video_source_parse(&response,"movie","device",s,error,sizeof(error));}
static void negotiation(void){
    xf_video_source source;char body[1800],copy[sizeof(valid)+40],*p;
    CHECK(xf_video_profile(body,sizeof(body),"user") && strstr(body,"mpeg1video") && strstr(body,"\"EnableDirectPlay\":false"));
    CHECK(!xf_video_profile(body,30,"user") && !xf_video_profile(body,sizeof(body),"user\"bad"));
    CHECK(parse(valid,&source) && source.width==1920 && source.height==1080 && source.runtime==3000000000ULL && source.transcoding);
    CHECK(!strncmp(source.path,"/Videos/movie/stream.mpeg?Static=false",36) && strstr(source.path,"AllowVideoStreamCopy=false") && strstr(source.path,"MaxAudioChannels=2") && !strstr(source.path,"api_key"));
    strcpy(copy,valid);p=strstr(copy,"SDR");memcpy(p,"HDR",3);CHECK(!parse(copy,&source));
    strcpy(copy,valid);p=strstr(copy,"http");memcpy(p,"hls ",4);CHECK(!parse(copy,&source));
    strcpy(copy,valid);p=strstr(copy,"File");memcpy(p,"Http",4);CHECK(!parse(copy,&source));
    strcpy(copy,valid);p=strstr(copy,"\"source\"");memcpy(p+1,"../bad",6);CHECK(!parse(copy,&source));
    CHECK(!parse("{}",&source) && !parse("{not json}",&source));
}
static void http64(void){
    const char *h="HTTP/1.1 200 OK\r\nContent-Length: 8589934592\r\n\r\n";
    xf_response_stream(&response,discard,NULL,UINT64_MAX);xf_response_feed(&response,h,strlen(h));
    CHECK(response.headers_done && response.remaining==8589934592ULL && !response.failed);
    response.streamed=UINT32_MAX;response.body_size=SIZE_MAX-1;xf_response_feed(&response,"ab",2);
    CHECK(response.streamed==4294967297ULL && response.remaining==8589934590ULL && response.body_size==SIZE_MAX && !response.failed);
    xf_response_eof(&response);CHECK(response.failed);
    h="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n100000000\r\n";
    xf_response_stream(&response,discard,NULL,UINT64_MAX);xf_response_feed(&response,h,strlen(h));CHECK(!response.failed && response.remaining==4294967296ULL);
    h="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\nX-End: yes\r\n\r\n";
    xf_response_stream(&response,discard,NULL,UINT64_MAX);xf_response_feed(&response,h,strlen(h));CHECK(response.complete && response.streamed==3 && !response.failed);
    h="HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n";
    xf_response_stream(&response,discard,NULL,UINT64_MAX);xf_response_feed(&response,h,strlen(h));CHECK(response.failed);
}
int main(void){framing();negotiation();http64();printf("Native MPEG-PS boundaries/backpressure/format rejection, negotiation and 64-bit HTTP: %u assertions passed\n",checks);return 0;}
