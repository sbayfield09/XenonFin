#ifndef XF_VIDEO_AUDIO_H
#define XF_VIDEO_AUDIO_H
#include "music.h"
#define XF_AV_STAMPS 128U
#define XF_AV_DMA_BYTES 8192U /* 42.667 ms maximum outstanding, 2048-byte descriptors */
typedef struct { uint64_t ordinal; int64_t base_pts; } xf_av_map;
typedef struct { uint64_t offset; int64_t pts; } xf_av_stamp;
typedef struct {
    xf_audio_decoder decoder;
    xf_audio_backend backend;
    xf_av_stamp stamps[XF_AV_STAMPS];
    xf_av_map maps[XF_AV_STAMPS];
    unsigned map_head,map_count,bridge,gap_events;
    uint64_t read_frames,late_frames,bridge_frames;
    unsigned stamp_head,stamp_count,enabled,aligned,started,rebuffering,failed;
    unsigned have_video_pts,have_audio_pts,underruns,queued,peak_pcm,peak_input;
    int64_t video_pts,audio_pts,initial_offset_us,timestamp_error_us;
    uint64_t submitted,played,pcm_frames,silence_frames,trim_frames,trimmed_frames,tail_frames;
    unsigned char block[2048],discard[4096];size_t block_used;
    char error[128];
} xf_av_audio;
void xf_av_audio_init(xf_av_audio *,const xf_audio_backend *);
int xf_av_audio_byte(void *,unsigned char);
size_t xf_av_audio_space(void *);
int xf_av_audio_stamp(void *,unsigned kind,uint64_t offset,int64_t pts90k);
void xf_av_audio_decode(xf_av_audio *,int eof);
/* Hardware clock: submitted minus ACTUAL unplayed DMA frames, not wall time. */
int xf_av_audio_clock(void *,uint64_t *us);
/* video_ready gates startup/recovery; allow=0 drains the short DMA queue only.
 * tail permits hardware-clocked silence after genuine audio EOF, for video tail. */
void xf_av_audio_output(xf_av_audio *,int allow,int video_ready,int tail);
int xf_av_audio_done(xf_av_audio *);
#endif
