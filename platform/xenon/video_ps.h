#ifndef XF_VIDEO_PS_H
#define XF_VIDEO_PS_H
#include <stddef.h>
#include <stdint.h>
#define XF_PS_RING (512U*1024U)
typedef struct {
    unsigned char ring[XF_PS_RING],packet[65542];
    size_t head,count,peak,packet_size,payload,payload_end;
    uint64_t received,video_bytes,packets,pts90k;
    unsigned ended,failed,seen_pack,eof,have_pts;
    char error[128];
} xf_video_ps;
void xf_video_ps_init(xf_video_ps *);
int xf_video_ps_byte(void *, unsigned char);
size_t xf_video_ps_space(void *);
/* bounded demux -> elementary sink. Returns -1 error, 0 waiting, 1 progress. */
int xf_video_ps_tick(xf_video_ps *, int (*sink)(unsigned char), size_t (*space)(void), int eof);
#endif
