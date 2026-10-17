#ifndef XF_JVIDEO_H
#define XF_JVIDEO_H
#include "video.h"
#include "video_api.h"
#include "video_ps.h"
#include "video_audio.h"
#include "native_net.h"
#include "browse.h"
enum xf_jvideo_state { XF_JV_IDLE, XF_JV_NEGOTIATING, XF_JV_STREAMING, XF_JV_STOPPING, XF_JV_FINISHED, XF_JV_ERROR };
enum xf_av_mode { XF_AV_BOTH, XF_AV_VIDEO_ONLY, XF_AV_AUDIO_ONLY };
enum xf_av_state { XF_AV_INITIAL_BUFFERING, XF_AV_BUFFERING=XF_AV_INITIAL_BUFFERING, XF_AV_PREROLL, XF_AV_PLAYING, XF_AV_REBUFFERING, XF_AV_PAUSED, XF_AV_ERROR, XF_AV_ENDED, XF_AV_STOPPED };
enum { XF_AV_HTTP_RX, XF_AV_DEMUX, XF_AV_AUDIO_ES, XF_AV_MP3, XF_AV_PCM, XF_AV_DMA_SUBMIT, XF_AV_DMA_CONSUME, XF_AV_VIDEO_DECODE, XF_AV_VIDEO_SCHEDULE, XF_AV_GPU_PRESENT, XF_AV_STAGE_COUNT };
typedef struct { uint64_t value; uint32_t changed_at; } xf_av_stage;
typedef struct { uint32_t at; unsigned state; } xf_av_transition;
typedef struct { uint64_t offset; int64_t pts; } xf_av_pts;
#define XF_AV_REBUFFERS 16U
typedef struct {
    uint32_t at,duration,pcm,audio_es,video_es,ps,video_queue,http_age;
    uint64_t service_gap_us,submit_age_us,received;
    char reason[72];
} xf_av_rebuffer;
typedef struct {
    xf_http_client stream,control;
    xf_video_ps ps;
    xf_av_audio audio;
    xf_video *player;xf_video_backend backend;
    xf_video_source source;xf_endpoint endpoint;
    char token[2049],user[65],device[65],item[65],title[181];
    char message[180],report_message[128];
    enum xf_jvideo_state state;
    unsigned pause_requested,report_due,mode,stopped,lease;
    enum xf_av_state av_state;
    xf_av_transition transitions[32]; unsigned transition_count;
    xf_av_stage stages[XF_AV_STAGE_COUNT];
    xf_av_rebuffer rebuffers[XF_AV_REBUFFERS];
    unsigned rebuffer_count,rebuffer_open;
    uint64_t last_submit_at,last_service_gap_us;
    xf_av_pts first_pts[2][30]; unsigned pts_count[2];
    char wait_reason[96],error_origin[24];
    unsigned service_calls,submit_calls;
    unsigned dma_register,dma_seen,dma_wraps,dma_overruns,dma_counter_faults;
    int terminal_http_code,terminal_http_done,terminal_http_complete,terminal_http_failed;
    char terminal_http_error[128];
    uint64_t terminal_played,terminal_submitted,terminal_video_pts,terminal_audio_us;
    unsigned terminal_dma_register;
    uint32_t stalled_ms;
    size_t last_pcm,last_dma;
    uint64_t service_at,max_service_gap_us;
    xf_time_stats refill_times,dma_times,clock_times;
    uint32_t drain_at,progress_at;uint64_t progress_value;
    /* First shutdown cause survives secondary drain/report failures. */
    char drain_caller[32],drain_message[180],drain_origin[24],drain_error[112];
    unsigned drain_line,drain_from,drain_av_from,drain_failed,drain_timed_out;
    int drain_first_queue,drain_last_queue;
    uint64_t drain_submitted,drain_played;
    unsigned control_kind,control_pending,session_expired,failed,decoder_end;
    unsigned sent_start,sent_stop,cleaned,attached,stream_status,report_status,kbps;
    unsigned start_status,progress_status,stop_status,cleanup_status,progress_reports,report_errors;
    uint32_t report_at,rate_at;uint64_t rate_bytes,network_us,demux_us;
    xf_time_stats network_times,demux_times,audio_times;
} xf_jvideo;
void xf_jvideo_init(xf_jvideo *,xf_video *);
void xf_jvideo_forget(xf_jvideo *);
void xf_jvideo_audio(xf_jvideo *,const xf_audio_backend *);
int xf_jvideo_mode(xf_jvideo *,unsigned mode);
const char *xf_jvideo_state_name(unsigned);
void xf_jvideo_service(xf_jvideo *,uint32_t now);
int xf_jvideo_export(const xf_jvideo *,const char *path);
void xf_jvideo_pause(xf_jvideo *,uint32_t now);
int xf_jvideo_active(const xf_jvideo *);
int xf_jvideo_play(xf_jvideo *,const xf_endpoint *,const char *device,const char *token,const char *user,const xf_item *,const xf_video_backend *,uint32_t);
void xf_jvideo_tick(xf_jvideo *,uint32_t now,int network_ready);
void xf_jvideo_stop(xf_jvideo *,uint32_t now);
#endif
