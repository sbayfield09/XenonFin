#include "video_audio.h"
#include <string.h>
#include <stdio.h>
static void fail(xf_av_audio *a,const char *text){if(!a->failed)snprintf(a->error,sizeof(a->error),"%s",text);a->failed=1;}
static void frame(void *context,uint64_t offset,uint64_t output_frame) {
    xf_av_audio *a=context;int marked=0;int64_t pts=0,expected;
    /* A PES PTS names the first frame STARTING in that PES, including when
     * the packet begins with a continuation of the previous MP3 frame. */
    while(a->stamp_count && a->stamps[a->stamp_head].offset<=offset) {
        pts=a->stamps[a->stamp_head].pts;marked=1;
        a->stamp_head=(a->stamp_head+1)%XF_AV_STAMPS;--a->stamp_count;
    }
    if(!marked)return;
    if(!a->have_audio_pts) {
        a->audio_pts=pts-(int64_t)(output_frame*90000/48000);a->have_audio_pts=1;
    }
    expected=a->audio_pts+(int64_t)(output_frame*90000/48000);
    a->timestamp_error_us=(pts-expected)*1000000/90000;
    if(a->timestamp_error_us< -100000){fail(a,"Backward MP3 timestamp discontinuity; reopen movie.");return;}
    if(a->timestamp_error_us>10000000){fail(a,"Forward MP3 timestamp gap exceeds 10 seconds.");return;}
    /* 90 kHz rounding is far below 1 ms. Honour real small timestamp
     * gaps as well, rather than permitting up to 100 ms cumulative drift. */
    if(a->timestamp_error_us>1000 || a->timestamp_error_us< -1000) {
        a->audio_pts=pts-(int64_t)(output_frame*90000/48000);++a->gap_events;
    }
    if(!a->map_count || a->maps[(a->map_head+a->map_count-1)%XF_AV_STAMPS].base_pts!=a->audio_pts) {
        if(a->map_count==XF_AV_STAMPS){fail(a,"Too many queued PCM timestamp changes.");return;}
        a->maps[(a->map_head+a->map_count)%XF_AV_STAMPS]=(xf_av_map){output_frame,a->audio_pts};++a->map_count;
    }
}
void xf_av_audio_init(xf_av_audio *a,const xf_audio_backend *backend) {
    memset(a,0,sizeof(*a));if(backend)a->backend=*backend;
    xf_audio_decoder_init(&a->decoder);a->decoder.low_latency=1;a->decoder.decode_budget=1;
    a->decoder.tag_checked=1; /* raw MPEG-PS audio, not an ID3-tagged music file */
    a->decoder.on_frame=frame;a->decoder.frame_context=a;
}
size_t xf_av_audio_space(void *context) {
    xf_av_audio *a=context;
    return !a->failed && a->enabled?xf_audio_space(&a->decoder):0;
}
int xf_av_audio_byte(void *context,unsigned char c) {
    xf_av_audio *a=context;
    if(!a->enabled){fail(a,"Unexpected audio in a video-only negotiation.");return 0;}
    return !a->failed && xf_audio_byte(&a->decoder,c);
}
int xf_av_audio_stamp(void *context,unsigned kind,uint64_t offset,int64_t pts) {
    xf_av_audio *a=context;
    if(!kind){if(!a->have_video_pts){a->video_pts=pts;a->have_video_pts=1;}return 1;}
    if(!a->enabled){fail(a,"Unexpected audio in a video-only negotiation.");return -1;}
    if(a->stamp_count==XF_AV_STAMPS)return 0; /* retain pending PES, no byte loss */
    a->stamps[(a->stamp_head+a->stamp_count)%XF_AV_STAMPS]=(xf_av_stamp){offset,pts};++a->stamp_count;return 1;
}
static void align(xf_av_audio *a) {
    int64_t difference;
    if(a->aligned || !a->have_video_pts || !a->have_audio_pts)return;
    difference=a->audio_pts-a->video_pts;
    if(difference>900000 || difference< -900000){fail(a,"Initial A/V timestamp offset exceeds 10 seconds.");return;}
    a->initial_offset_us=difference*1000000/90000;
    /* Preserve every decoded sample, including audio preceding the first
     * video PTS. Hold video until the consumed-audio clock reaches video zero;
     * never trim PCM or change its pitch to catch a picture deadline. */
    a->origin_pts=difference<0?a->audio_pts:a->video_pts;
    a->clock_offset_us=difference<0?a->initial_offset_us:0;
    if(difference>0)a->silence_frames=(uint64_t)difference*48000/90000;
    if(!a->map_count){a->maps[0]=(xf_av_map){0,a->audio_pts};a->map_count=1;}
    a->aligned=1;
}
void xf_av_audio_decode(xf_av_audio *a,int eof) {
    if(!a->enabled || a->failed)return;
    xf_audio_decode_tick(&a->decoder,eof);
    if(a->decoder.failed){++a->decoder_errors;fail(a,a->decoder.error);return;}
    if(a->decoder.pcm_size>a->peak_pcm)a->peak_pcm=(unsigned)a->decoder.pcm_size;
    if(a->decoder.input_size>a->peak_input)a->peak_input=(unsigned)a->decoder.input_size;
    align(a);

}
int xf_av_audio_clock(void *context,uint64_t *us) {
    xf_av_audio *a=context;uint64_t played;int pending;int64_t relative;
    if(!a->enabled || !a->backend.queued){*us=0;return 0;}
    pending=a->backend.queued(a->backend.context);
    a->queue_reading=pending;a->queue_valid=pending>=0 && pending<=65536;
    if(pending<0 || pending>65536){fail(a,"Invalid audio DMA queue reading.");*us=a->played*1000000/48000;return 0;}
    a->queued=(unsigned)pending;
    played=a->submitted> a->queued/4?a->submitted-(a->queued+3)/4:0;
    if(played>a->played)a->played=played;
    relative=(int64_t)(a->played*1000000/48000)+a->clock_offset_us;
    *us=relative>0?(uint64_t)relative:0;
    return relative>=0 && a->started && !a->failed && !(a->rebuffering && !a->queued);
}
void xf_av_audio_output(xf_av_audio *a,int allow,int video_ready,int tail) {
    unsigned budget;uint64_t us;
    if(!a->enabled || a->failed)return;
    xf_av_audio_clock(a,&us);
    if(!allow || (!a->aligned && !(a->bridge && a->have_video_pts)) || a->failed)return;
    if(a->decoder.pcm_size>=XF_AV_RECOVER_PCM || (a->decoder.finished && a->decoder.pcm_size))a->bridge=0;
    if(a->started && !a->queued && !a->decoder.finished && !a->bridge && !a->rebuffering && a->decoder.pcm_size<XF_AV_RECOVER_PCM) {
        ++a->underruns;a->rebuffering=1;
    }
    if(!a->started || a->rebuffering) {
        if(!video_ready || (!a->bridge && !a->silence_frames && a->decoder.pcm_size<(a->started?XF_AV_RECOVER_PCM:XF_AV_START_PCM) && !a->decoder.finished))return;
        a->rebuffering=0;
    }
    for(budget=0;budget<4 && a->queued+sizeof(a->block)<=XF_AV_DMA_BYTES;++budget) {
        size_t left=sizeof(a->block)-a->block_used,n;
        if(a->decoder.pcm_size && a->map_count) {
            xf_av_map *map;uint64_t next=a->submitted+a->block_used/4;int64_t desired;size_t available=a->decoder.pcm_size;
            while(a->map_count>1 && a->maps[(a->map_head+1)%XF_AV_STAMPS].ordinal<=a->read_frames){a->map_head=(a->map_head+1)%XF_AV_STAMPS;--a->map_count;}
            map=&a->maps[a->map_head];desired=(map->base_pts-a->origin_pts)*48000/90000+(int64_t)a->read_frames;
            if(a->map_count>1) {
                uint64_t distance=a->maps[(a->map_head+1)%XF_AV_STAMPS].ordinal-a->read_frames;
                if(distance<available/4)available=(size_t)distance*4;
            }
            if(desired<(int64_t)next) {
                fail(a,"Audio PTS overlaps already submitted PCM; no samples discarded. Reopen stream.");return;
            }
            if(desired>(int64_t)next) {
                uint64_t gap=(uint64_t)desired-next;n=left;if(n/4>gap)n=(size_t)gap*4;
                memset(a->block+a->block_used,0,n);a->block_used+=n;
                if(a->silence_frames>=n/4)a->silence_frames-=n/4;
            } else {
                n=available;if(n>left)n=left;
                n=xf_audio_pcm_take(&a->decoder,a->block+a->block_used,n);a->block_used+=n;a->pcm_frames+=n/4;a->read_frames+=n/4;
            }
        }
        if(a->block_used<sizeof(a->block)) {
            /* A discontinuity may lie inside this block: continue filling
             * without padding over timestamped real PCM or leading silence. */
            if(a->decoder.pcm_size && a->block_used<sizeof(a->block))continue;
            if(!a->decoder.finished && !a->bridge)return;
            if(!a->block_used && (!tail || (a->tail_limit_frames && a->submitted>=a->tail_limit_frames)))return;
            left=sizeof(a->block)-a->block_used;memset(a->block+a->block_used,0,left);
            if(a->bridge)a->bridge_frames+=left/4;else a->tail_frames+=left/4;a->block_used=sizeof(a->block);
        }
        if(a->backend.available(a->backend.context)<(int)(sizeof(a->block)+2048))return;
        if(!a->backend.submit(a->backend.context,a->block,sizeof(a->block))){fail(a,"PCM output rejected submission.");return;}a->submitted+=sizeof(a->block)/4;
        a->queued+=sizeof(a->block);a->block_used=0;a->started=1;
    }
}
int xf_av_audio_done(xf_av_audio *a) {
    uint64_t us;xf_av_audio_clock(a,&us);
    return !a->enabled || (a->decoder.finished && !a->decoder.pcm_size && !a->block_used && !a->queued);
}
