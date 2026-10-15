#ifndef XF_VIDEO_H
#define XF_VIDEO_H
#include "video_decode.h"
#include "timing.h"
#include "video_sync.h"
#include <stdio.h>
#define XF_VIDEO_QUEUE 4U
/* GPU time is a fence observation upper bound; poll uncertainty is explicit. */
typedef struct {
    uint64_t conversion_us, upload_us, commands_us, gpu_observed_us, gpu_poll_gap_us, copy_us;
    unsigned prepared, gpu_completed, presented;
} xf_video_render_stats;
typedef struct {
    int (*begin)(void *, const xf_video_info *, char *, size_t);
    int (*submit)(void *, const xf_video_image *, char *, size_t);
    int (*poll)(void *, char *, size_t); /* 0 preparing, 1 ready in PRIVATE memory, -1 error */
    void (*end)(void *);
    uint64_t (*microseconds)(void *);
    const char *format;
    void *context;
    int (*present)(void *, char *, size_t); /* publish a ready picture only */
    const xf_video_render_stats *stats;
    int (*vblank)(void *); /* single read, never wait */
} xf_video_backend;
enum xf_video_state { XF_VIDEO_IDLE, XF_VIDEO_LOADING, XF_VIDEO_PLAYING, XF_VIDEO_ENDED, XF_VIDEO_ERROR };
enum xf_video_drop_reason { XF_DROP_LATE, XF_DROP_OVERFLOW, XF_DROP_DECODE, XF_DROP_GPU, XF_DROP_EOS, XF_DROP_REASONS };
#define XF_VIDEO_TRACE 4096U
typedef struct { uint64_t pts, timeline, lateness, processing, span; unsigned reason, occupancy; } xf_video_trace;
typedef struct { unsigned char data[XF_VIDEO_FRAME_BYTES]; uint64_t pts, work_us; } xf_video_slot;
typedef struct {
    xf_video_backend backend;
    const xf_video_codec *codec;
    xf_video_info info;
    xf_video_slot queue[XF_VIDEO_QUEUE];
    FILE *file;
    unsigned char *bytes;
    size_t size, loaded;
    unsigned head, count, decoded, rendered, dropped, max_occupancy, fps_milli;
    uint64_t timeline_us, decode_us, max_decode_us;
    uint64_t last_clock_us, pending_pts, queue_copy_us, ui_us, wait_us, loop_us;
    unsigned next_sync_mode;
    uint64_t next_late_threshold_us;
    uint64_t origin_us, origin_pts, paused_us, pause_at, last_decoded_pts, last_present_pts;
    uint64_t pending_work_us, pending_source_work_us, pending_span_us, prepare_at, decode_estimate_us, late_threshold_us, last_present_at;
    int64_t drift_us;
    unsigned drop_reason[XF_DROP_REASONS], decode_failures, gpu_stalls, eof_events, full_ticks, clock_faults, cancelled;
    unsigned pending_stall, trace_count, trace_overwritten;
    xf_video_trace trace[XF_VIDEO_TRACE];
    xf_time_stats processing, frame_span, lateness, drop_lateness, intervals, ui_times, loop_times;
    xf_video_sync sync;
    unsigned ui_frames, loop_count, starved, synthetic;
    uint32_t fps_at, fps_frames;
    unsigned paused, clock_started, eof, pending, ready, backend_started;
    enum xf_video_state state;
    char path[128], message[180];
} xf_video;
void xf_video_init(xf_video *, const xf_video_codec *);
int xf_video_start(xf_video *, const char *path, const xf_video_backend *);
int xf_video_start_colours(xf_video *, const xf_video_backend *);
unsigned xf_video_wait_us(const xf_video *);
int xf_video_options(xf_video *, unsigned sync, uint64_t lateness_us);
int xf_video_export(const xf_video *, const char *path);
int xf_video_ui_safe(xf_video *);
void xf_video_service(xf_video *);
void xf_video_tick(xf_video *, uint32_t now);
void xf_video_pause(xf_video *, uint32_t now);
void xf_video_stop(xf_video *);
int xf_video_active(const xf_video *);
#endif
