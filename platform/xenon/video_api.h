#ifndef XF_VIDEO_API_H
#define XF_VIDEO_API_H
#include "protocol.h"
typedef struct {
    char source_id[65],session_id[65],path[1501];
    char container[40],video_codec[40],audio_codec[40];
    unsigned sources,width,height,video_index;
    uint64_t runtime;
    int transcoding;
} xf_video_source;
int xf_video_profile(char *,size_t,const char *user);
int xf_video_source_parse(const xf_response *,const char *item,const char *device,xf_video_source *,char *,size_t);
#endif
