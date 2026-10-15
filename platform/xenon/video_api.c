#include "video_api.h"
#include "music_api.h"
#include <string.h>
#include <stdio.h>
static int field(xf_json_ref o,const char *key,char *text,size_t cap) {return xf_json_string(o.data,o.size,key,text,cap)==1;}
static int flag(xf_json_ref o,const char *key) {xf_json_ref r;int b=0;return xf_json_get(o,key,&r)==1 && xf_json_bool(r,&b) && b;}
static uint64_t number(xf_json_ref o,const char *key) {xf_json_ref r;uint64_t n=0;if(xf_json_get(o,key,&r)==1)xf_json_uint(r,&n);return n;}
int xf_video_profile(char *out,size_t cap,const char *user) {
    int n;if(!xf_music_id(user))return 0;
    n=snprintf(out,cap,"{\"UserId\":\"%s\",\"IsPlayback\":true,\"AutoOpenLiveStream\":false,\"StartTimeTicks\":0,"
        "\"EnableDirectPlay\":false,\"EnableDirectStream\":false,\"EnableTranscoding\":true,\"AllowVideoStreamCopy\":false,\"AllowAudioStreamCopy\":false,"
        "\"MaxStreamingBitrate\":3000000,\"DeviceProfile\":{\"Name\":\"XenonFin MPEG1 silent\",\"MaxStreamingBitrate\":3000000,"
        "\"DirectPlayProfiles\":[],\"TranscodingProfiles\":[{\"Type\":\"Video\",\"Container\":\"mpeg\",\"Protocol\":\"http\",\"VideoCodec\":\"mpeg1video\",\"AudioCodec\":\"mp2\",\"MaxAudioChannels\":\"2\",\"Context\":\"Streaming\"}],"
        "\"CodecProfiles\":[{\"Type\":\"Video\",\"Codec\":\"mpeg1video\",\"Conditions\":["
        "{\"Condition\":\"LessThanEqual\",\"Property\":\"Width\",\"Value\":\"720\"},"
        "{\"Condition\":\"LessThanEqual\",\"Property\":\"Height\",\"Value\":\"480\"},"
        "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoFramerate\",\"Value\":\"30\"}]}],\"SubtitleProfiles\":[]}}",user);
    return n>=0 && (size_t)n<cap;
}
int xf_video_source_parse(const xf_response *response,const char *item,const char *device,xf_video_source *out,char *error,size_t cap) {
    xf_json_ref root={response->body,response->body_size},array,sources[8];size_t count,i;
    char session[65];xf_wipe(out,sizeof(*out));
    if(!xf_music_id(item) || !xf_music_id(device) || response->status!=200 || !response->complete || response->failed ||
       !field(root,"PlaySessionId",session,sizeof(session)) || !xf_music_id(session) ||
       xf_json_get(root,"MediaSources",&array)!=1 || !xf_json_elements(array,0,sources,8,&count) || !count || count>8) {
        snprintf(error,cap,"Invalid playback information or too many sources.");return 0;
    }
    for(i=0;i<count;++i) {
        xf_json_ref streams,entries[32];size_t n,j;char protocol[32],container[40],source[65];int video=0;
        xf_wipe(out,sizeof(*out));
        if(!flag(sources[i],"SupportsTranscoding") || !field(sources[i],"Id",source,sizeof(source)) || !xf_music_id(source) ||
           !field(sources[i],"Protocol",protocol,sizeof(protocol)) || strcmp(protocol,"File") || flag(sources[i],"IsInfiniteStream") ||
           !field(sources[i],"TranscodingSubProtocol",protocol,sizeof(protocol)) || strcmp(protocol,"http") ||
           !field(sources[i],"TranscodingContainer",container,sizeof(container)) || strcmp(container,"mpeg") ||
           xf_json_get(sources[i],"MediaStreams",&streams)!=1 || !xf_json_elements(streams,0,entries,32,&n) || n>32)continue;
        out->runtime=number(sources[i],"RunTimeTicks");
        field(sources[i],"Container",out->container,sizeof(out->container));
        for(j=0;j<n;++j) {
            char type[24],range[32];
            if(!field(entries[j],"Type",type,sizeof(type)))continue;
            if(!strcmp(type,"Video") && !flag(entries[j],"IsAttachedPic") && !video) {
                field(entries[j],"Codec",out->video_codec,sizeof(out->video_codec));
                out->width=(unsigned)number(entries[j],"Width");out->height=(unsigned)number(entries[j],"Height");out->video_index=(unsigned)number(entries[j],"Index");
                if(field(entries[j],"VideoRangeType",range,sizeof(range)) && strcmp(range,"SDR")) {video=-1;break;}
                if(field(entries[j],"VideoRange",range,sizeof(range)) && strcmp(range,"SDR")) {video=-1;break;}
                video=1;
            } else if(!strcmp(type,"Audio")) field(entries[j],"Codec",out->audio_codec,sizeof(out->audio_codec));
        }
        if(video!=1 || out->video_index>255)continue;
        snprintf(out->source_id,sizeof(out->source_id),"%s",source);snprintf(out->session_id,sizeof(out->session_id),"%s",session);
        /* Documented progressive endpoint. Never use Static=true/original or
         * an arbitrary returned URL. Tokens stay exclusively in auth headers. */
        {
            int rc=snprintf(out->path,sizeof(out->path),"/Videos/%s/stream.mpeg?Static=false&MediaSourceId=%s&DeviceId=%s&PlaySessionId=%s"
                "&VideoCodec=mpeg1video&VideoStreamIndex=%u&AudioCodec=mp2&AudioBitRate=64000&MaxAudioChannels=2&SubtitleStreamIndex=-1"
                "&EnableAutoStreamCopy=false&AllowVideoStreamCopy=false&AllowAudioStreamCopy=false"
                "&MaxWidth=720&MaxHeight=480&Framerate=30&VideoBitRate=2500000&RequireNonAnamorphic=true",item,source,device,session,out->video_index);
            if(rc<0 || (size_t)rc>=sizeof(out->path))break;
        }
        out->sources=(unsigned)count;out->transcoding=1;return 1;
    }
    snprintf(error,cap,"No local SDR source with progressive MPEG transcoding permission. Check server/profile.");return 0;
}
