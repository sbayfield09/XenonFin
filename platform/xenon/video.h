#ifndef XF_VIDEO_H
#define XF_VIDEO_H
#include "video_decode.h"
#include <stdio.h>
#define XF_VIDEO_QUEUE 2U
typedef struct {
    int (*begin)(void *, const xf_video_info *, char *, size_t);
    int (*submit)(void *, const xf_video_image *, char *, size_t);
    int (*poll)(void *, char *, size_t); /* 0 pending, 1 copied/presented, -1 error */
    void (*end)(void *);
    uint64_t (*microseconds)(void *);
    const char *format;
    void *context;
} xf_video_backend;
enum xf_video_state { XF_VIDEO_IDLE, XF_VIDEO_LOADING, XF_VIDEO_PLAYING, XF_VIDEO_ENDED, XF_VIDEO_ERROR };
typedef struct { unsigned char data[XF_VIDEO_FRAME_BYTES]; uint64_t pts; } xf_video_slot;
typedef struct {
    xf_video_backend backend;
    const xf_video_codec *codec;
    xf_video_info info;
    xf_video_slot queue[XF_VIDEO_QUEUE];
    FILE *file;
    unsigned char *bytes;
    size_t size, loaded;
    unsigned head, count, decoded, rendered, dropped, max_occupancy, fps_milli;
    uint64_t timeline_us, decode_us, max_decode_us, render_us, submitted_at;
    uint32_t last_ms, fps_at, fps_frames;
    unsigned paused, clock_started, eof, pending, backend_started;
    enum xf_video_state state;
    char path[128], message[180];
} xf_video;
void xf_video_init(xf_video *, const xf_video_codec *);
int xf_video_start(xf_video *, const char *path, const xf_video_backend *);
void xf_video_tick(xf_video *, uint32_t now);
void xf_video_pause(xf_video *, uint32_t now);
void xf_video_stop(xf_video *);
int xf_video_active(const xf_video *);
#endif
