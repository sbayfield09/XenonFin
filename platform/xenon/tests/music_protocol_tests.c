#include "../music_api.h"
#include "../audio_decode.h"
#include "../text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"music protocol line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static xf_response response;
static xf_audio_decoder decoder;
static int receive_byte(void *context, unsigned char c) { ++*(unsigned *)context; return c == 'a'; }
int main(void) {
    char body[1500], text[100], request[XF_REQUEST_LIMIT], error[128];
    xf_music_source source; xf_endpoint ep; size_t at; unsigned n = 0, i;
    const char *id = "11111111111111111111111111111111";
    CHECK(xf_music_profile(body, sizeof(body), id) && strstr(body,"\"Container\":\"mp3\"") && !strstr(body,"VideoCodec"));
    CHECK(!xf_music_profile(body, 20, id)); CHECK(!xf_music_profile(body, sizeof(body), "bad\"id"));
    xf_response_init(&response); response.status = 200; response.complete = 1;
    strcpy(response.body, "{\"PlaySessionId\":\"session\",\"MediaSources\":[{\"Id\":\"source\",\"Protocol\":\"File\",\"Container\":\"mp3\",\"SupportsDirectPlay\":true,\"MediaStreams\":[{\"Type\":\"Audio\",\"Codec\":\"mp3\",\"SampleRate\":44100,\"Channels\":1}]}]}");
    response.body_size = strlen(response.body);
    CHECK(xf_music_source_parse(&response,id,&source)&&!source.transcode&&strstr(source.path,"Static=true")&&!strstr(source.path,"api_key"));
    CHECK(xf_music_report(body,sizeof(body),id,&source,123456789,1)&&strstr(body,"\"PositionTicks\":123456789")&&strstr(body,"\"IsPaused\":true")&&strstr(body,"\"CanSeek\":false"));
    strcpy(response.body,"{\"PlaySessionId\":\"session\",\"MediaSources\":[{\"Id\":\"source\",\"SupportsTranscoding\":true,\"TranscodingSubProtocol\":\"http\",\"TranscodingUrl\":\"/audio/item/stream.mp3?api_key=SECRET&DeviceId=device&AudioCodec=mp3\"}]}");
    response.body_size = strlen(response.body); CHECK(xf_music_source_parse(&response,id,&source)&&source.transcode&&!strstr(source.path,"SECRET")&&!strstr(source.path,"api_key")&&strstr(source.path,"?DeviceId=device&AudioCodec=mp3"));
    strcpy(response.body,"{\"PlaySessionId\":\"session\",\"MediaSources\":[{\"Id\":\"source\",\"SupportsTranscoding\":true,\"TranscodingSubProtocol\":\"http\",\"TranscodingUrl\":\"http://other/audio/stream.mp3\"}]}");
    response.body_size = strlen(response.body); CHECK(!xf_music_source_parse(&response,id,&source));
    CHECK(xf_endpoint_parse("127.0.0.1",&ep,error,sizeof(error)));
    CHECK(xf_api_request(request,sizeof(request),&ep,"device","token","POST","/Sessions/Playing","{}",0)>0&&strstr(request,"Content-Length: 2\r\n\r\n{}"));
    CHECK(xf_api_request(request,sizeof(request),&ep,"device","token","DELETE","/Videos/ActiveEncodings?DeviceId=device",NULL,0)>0);
    CHECK(xf_api_request(request,sizeof(request),&ep,"device","token","GET\r\n","/Items",NULL,0)<0&&!request[0]);
    CHECK(xf_api_request(request,sizeof(request),&ep,"device","token","GET","/Audio/id/stream.mp3",NULL,1)>0&&strstr(request,"Accept: audio/mpeg"));
    xf_response_stream(&response,receive_byte,&n,200000);
    xf_response_feed(&response,"HTTP/1.1 200 OK\r\nContent-Length: 70000\r\n\r\n",strlen("HTTP/1.1 200 OK\r\nContent-Length: 70000\r\n\r\n"));
    for(i=0;i<70000;++i) xf_response_feed(&response,"a",1);
    CHECK(response.complete&&!response.failed&&response.body_size==70000&&n==70000&&!response.body[0]);
    xf_response_stream(&response,receive_byte,&n,100000);
    xf_response_feed(&response,"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\naaa\r\n0\r\n\r\n",strlen("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\naaa\r\n0\r\n\r\n"));
    CHECK(response.complete&&!response.failed&&response.body_size==3);
    xf_response_stream(&response,receive_byte,&n,10);
    xf_response_feed(&response,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 1\r\n\r\na",strlen("HTTP/1.1 401 Unauthorized\r\nContent-Length: 1\r\n\r\na"));
    CHECK(response.failed&&response.status==401);
    xf_audio_decoder_init(&decoder); for(i=0;i<200;++i) CHECK(xf_audio_byte(&decoder,'x'));
    xf_audio_decode_tick(&decoder,1); CHECK(decoder.failed&&!decoder.mp3_frames&&!decoder.pcm_size);
    xf_audio_decoder_init(&decoder); for(i=0;i<XF_COMPRESSED_CAP;++i) CHECK(xf_audio_byte(&decoder,'x'));
    CHECK(!xf_audio_space(&decoder)&&!xf_audio_byte(&decoder,0));
    xf_audio_decoder_init(&decoder);
    xf_text_cell(text,sizeof(text),"A very long title",10); CHECK(!strcmp(text,"A very ..."));
    xf_text_cell(text,sizeof(text),"Short",10); CHECK(!strcmp(text,"Short     "));
    xf_text_cell(text,sizeof(text),"a\x1b[31m \xc3\xa9",12); CHECK(!strchr(text,27)&&strchr(text,'?'));
    at=0; CHECK(xf_text_line("one two three four",&at,9,text,sizeof(text))&&!strcmp(text,"one two"));
    CHECK(xf_text_line("one two three four",&at,9,text,sizeof(text))&&!strcmp(text,"three"));
    CHECK(xf_text_line("one two three four",&at,9,text,sizeof(text))&&!strcmp(text,"four"));
    CHECK(xf_text_lines("abcdefghijk",4)==3);
    printf("Native music API/stream framing/decoder bounds/layout: %u assertions passed\n",checks);return 0;
}
