#ifndef XF_VIDEO_SYNC_H
#define XF_VIDEO_SYNC_H
#include <stdint.h>
enum xf_video_sync_mode { XF_SYNC_OFF, XF_SYNC_ON, XF_SYNC_HALF };
typedef struct {
    unsigned mode, level, edges, released, samples, sample_at, periods[16];
    unsigned period_us, faults, half_rejected, fallbacks;
    uint64_t last_edge_us, wait_at, wait_us;
} xf_video_sync;
void xf_video_sync_sample(xf_video_sync *, uint64_t now, int blank);
int xf_video_sync_ready(xf_video_sync *, uint64_t now);
#endif
