#ifndef XF_VIDEO_PS_H
#define XF_VIDEO_PS_H
#include <stddef.h>
#include <stdint.h>
#define XF_PS_RING (512U*1024U)
typedef struct {
    unsigned char ring[XF_PS_RING],packet[65542];
    size_t head,count,peak,packet_size,payload,payload_end;
    uint64_t received,video_bytes,packets,pts90k;
    unsigned ended,failed,seen_pack,eof,have_pts,discard_video,optional_initial_pts;
    int64_t last_video_pts,last_audio_pts;
    unsigned payload_kind,stamp_pending;
    uint64_t audio_bytes,audio_packets;
    int64_t timestamp,reference_pts,first_video_pts,first_audio_pts;
    unsigned have_reference,have_video_pts,have_audio_pts;
    int (*audio_byte)(void *,unsigned char);
    size_t (*audio_space)(void *);
    int (*stamp)(void *,unsigned kind,uint64_t offset,int64_t pts90k);
    void *audio_context;
    void *stamp_context;
    char error[128];
} xf_video_ps;
void xf_video_ps_init(xf_video_ps *);
/* Optional audio receiver. kind 0=E0 video, 1=C0 audio. PTS is unwrapped on
 * one shared 33-bit timeline; offset is that elementary stream's byte offset. */
void xf_video_ps_audio(xf_video_ps *,int (*)(void *,unsigned char),size_t (*)(void *),void *);
void xf_video_ps_timestamps(xf_video_ps *,int (*)(void *,unsigned,uint64_t,int64_t),void *);
int xf_video_ps_byte(void *, unsigned char);
size_t xf_video_ps_space(void *);
/* bounded demux -> elementary sink. Returns -1 error, 0 waiting, 1 progress. */
int xf_video_ps_tick(xf_video_ps *, int (*sink)(unsigned char), size_t (*space)(void), int eof);
#endif
