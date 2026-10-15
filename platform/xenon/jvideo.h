#ifndef XF_JVIDEO_H
#define XF_JVIDEO_H
#include "video.h"
#include "video_api.h"
#include "video_ps.h"
#include "video_audio.h"
#include "native_net.h"
#include "browse.h"
enum xf_jvideo_state { XF_JV_IDLE, XF_JV_NEGOTIATING, XF_JV_STREAMING, XF_JV_STOPPING, XF_JV_FINISHED, XF_JV_ERROR };
typedef struct {
    xf_http_client stream,control;
    xf_video_ps ps;
    xf_av_audio audio;
    xf_video *player;xf_video_backend backend;
    xf_video_source source;xf_endpoint endpoint;
    char token[2049],user[65],device[65],item[65],title[181];
    char message[180],report_message[128];
    enum xf_jvideo_state state;
    unsigned pause_requested,report_due;
    uint32_t drain_at,progress_at;uint64_t progress_value;
    unsigned control_kind,control_pending,session_expired,failed,decoder_end;
    unsigned sent_start,sent_stop,cleaned,attached,stream_status,report_status,kbps;
    unsigned start_status,progress_status,stop_status,cleanup_status,progress_reports,report_errors;
    uint32_t report_at,rate_at;uint64_t rate_bytes,network_us,demux_us;
    xf_time_stats network_times,demux_times,audio_times;
} xf_jvideo;
void xf_jvideo_init(xf_jvideo *,xf_video *);
void xf_jvideo_forget(xf_jvideo *);
void xf_jvideo_audio(xf_jvideo *,const xf_audio_backend *);
void xf_jvideo_pause(xf_jvideo *,uint32_t now);
int xf_jvideo_active(const xf_jvideo *);
int xf_jvideo_play(xf_jvideo *,const xf_endpoint *,const char *device,const char *token,const char *user,const xf_item *,const xf_video_backend *,uint32_t);
void xf_jvideo_tick(xf_jvideo *,uint32_t now,int network_ready);
void xf_jvideo_stop(xf_jvideo *,uint32_t now);
#endif
