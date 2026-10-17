#include "jvideo.h"
#include "music_api.h"
#include <stdio.h>
#include <string.h>
static void transition(xf_jvideo *,unsigned,uint32_t);
enum { JV_NEGOTIATE=1,JV_START,JV_PROGRESS,JV_STOP,JV_CLEANUP };
void xf_jvideo_init(xf_jvideo *m,xf_video *v) {memset(m,0,sizeof(*m));m->player=v;xf_http_init(&m->stream);xf_http_init(&m->control);xf_av_audio_init(&m->audio,NULL);}
void xf_jvideo_audio(xf_jvideo *m,const xf_audio_backend *audio) {xf_av_audio_init(&m->audio,audio);}
int xf_jvideo_active(const xf_jvideo *m) {return m->state>=XF_JV_NEGOTIATING && m->state<=XF_JV_STOPPING;}
void xf_jvideo_forget(xf_jvideo *m) {
    unsigned mode=m->mode,lease=m->lease;xf_audio_backend audio=m->audio.backend;
    if(lease && audio.queued(audio.context)==0 && xf_audio_release(&audio))lease=0;
    xf_video *v=m->player;int owned=xf_jvideo_active(m);
    xf_http_cancel(&m->stream);xf_http_cancel(&m->control);if(owned)xf_video_stop(v);
    xf_wipe(m,sizeof(*m));xf_jvideo_init(m,v);xf_jvideo_audio(m,&audio);m->mode=mode;m->lease=lease;
}
static void control(xf_jvideo *m,unsigned kind,const char *method,const char *path,const char *body,uint32_t now) {
    xf_http_api(&m->control,&m->endpoint,m->device,m->token,method,path,body,now);m->control_kind=kind;m->control_pending=1;
}
static void finish_at(xf_jvideo *m,int failed,const char *message,uint32_t now,const char *caller,unsigned line) {
    if(m->state==XF_JV_STOPPING)return;
    m->drain_at=now;m->drain_line=line;m->drain_from=m->state;m->drain_av_from=m->av_state;m->drain_failed=failed;
    snprintf(m->drain_caller,sizeof(m->drain_caller),"%s",caller);
    if(message)snprintf(m->message,sizeof(m->message),"%s",message);
    m->terminal_http_code=m->stream.error_code;m->terminal_http_done=m->stream.done;
    m->terminal_http_complete=m->stream.response.complete;m->terminal_http_failed=m->stream.response.failed;
    snprintf(m->terminal_http_error,sizeof(m->terminal_http_error),"%s",m->stream.error);
    m->last_pcm=m->audio.decoder.pcm_size;m->last_dma=m->audio.queued;
    {uint64_t position=0;if(m->audio.enabled)xf_av_audio_clock(&m->audio,&position);m->terminal_audio_us=position;}
    m->terminal_played=m->audio.played;m->terminal_submitted=m->audio.submitted;
    m->terminal_video_pts=m->player->last_present_pts;m->terminal_dma_register=m->dma_register;
    if(failed && !m->error_origin[0])snprintf(m->error_origin,sizeof(m->error_origin),"playback control");
    snprintf(m->drain_message,sizeof(m->drain_message),"%s",m->message);
    snprintf(m->drain_origin,sizeof(m->drain_origin),"%s",m->error_origin);
    m->drain_first_queue=m->drain_last_queue=m->audio.queue_reading;
    m->drain_submitted=m->audio.submitted;m->drain_played=m->audio.played;
    m->failed|=failed;transition(m,failed?XF_AV_ERROR:m->stopped?XF_AV_STOPPED:XF_AV_ENDED,now);
    if(m->stream.active)xf_http_cancel(&m->stream);
    if(m->control_pending && (m->control_kind==JV_NEGOTIATE || m->control_kind==JV_PROGRESS)) {
        xf_http_cancel(&m->control);m->control_pending=0;xf_wipe(&m->control.response,sizeof(m->control.response));
    }
    if(m->player->state!=XF_VIDEO_ENDED && m->player->state!=XF_VIDEO_ERROR)xf_video_stop(m->player);
    m->audio.decoder.input_size=m->audio.decoder.pcm_size=m->audio.block_used=0;
    m->audio.decoder.finished=1;
    m->pause_requested=0;
    m->state=XF_JV_STOPPING;
}
#define finish(m,failed,message,now) finish_at(m,failed,message,now,__func__,__LINE__)
const char *xf_jvideo_state_name(unsigned state) {
    static const char *names[]={"INITIAL_BUFFERING","INITIAL_BUFFERING","PLAYING","REBUFFERING","PAUSED","ERROR","ENDED","STOPPED"};
    return state<sizeof(names)/sizeof(*names)?names[state]:"UNKNOWN";
}
int xf_jvideo_mode(xf_jvideo *m,unsigned mode) {
    if(xf_jvideo_active(m) || mode>XF_AV_AUDIO_ONLY)return 0;
    m->mode=mode;return 1;
}
static void transition(xf_jvideo *m,unsigned state,uint32_t now) {
    if(m->transition_count && (unsigned)m->av_state==state)return;
    m->av_state=(enum xf_av_state)state;
    m->transitions[m->transition_count++%32]=(xf_av_transition){now,state};
}
static int stamp(void *context,unsigned kind,uint64_t offset,int64_t pts) {
    xf_jvideo *m=context;int rc=m->audio.enabled?xf_av_audio_stamp(&m->audio,kind,offset,pts):1;
    if(rc>0 && m->pts_count[kind]<30)m->first_pts[kind][m->pts_count[kind]++]=(xf_av_pts){offset,pts};
    return rc;
}
static const char *waiting(const xf_jvideo *m) {
    const xf_av_audio *a=&m->audio;const xf_video *v=m->player;
    if(a->queued && m->stalled_ms>=1000)return "DMA: queued samples not consuming";
    if(m->pause_requested)return a->queued?"pause: draining DMA":"user pause";
    if(!m->stream.response.headers_done)return "HTTP: waiting for headers";
    if(a->enabled && (!a->have_video_pts || !a->have_audio_pts))return "PTS: waiting for initial V/A anchor";
    if(m->ps.stamp_pending && a->stamp_count==XF_AV_STAMPS)return "demux: audio timestamp queue full";
    if(m->ps.payload<m->ps.payload_end) {
        if(m->ps.payload_kind && !xf_audio_space((void *)&a->decoder))return "demux: audio ES full";
        if(!m->ps.payload_kind && !m->ps.discard_video && !xf_mpeg1_stream_space())return "demux: video ES full (head of line)";
    }
    if(a->enabled && !a->queued) {
        if(a->decoder.pcm_size>=16384)return "preroll: waiting for decoded video";
        if(a->decoder.input_size)return "MP3: partial frame / PCM preroll";
        if(!m->ps.count && !m->decoder_end)return "network: awaiting audio bytes";
        return "demux: awaiting audio PES";
    }
    if(v->pending && !v->ready)return "GPU: fence pending (nonblocking)";
    if(v->pending && v->pending_pts>v->timeline_us)return "video: early frame, audio deadline";
    if(v->input_wait && !m->ps.count)return "network: awaiting video bytes";
    return "running";
}
static void rebuffer_event(xf_jvideo *m,uint32_t now) {
    xf_av_audio *a=&m->audio;xf_av_rebuffer *r=&m->rebuffers[m->rebuffer_count++%XF_AV_REBUFFERS];
    uint64_t us=m->backend.microseconds(m->backend.context);const char *reason;
    memset(r,0,sizeof(*r));r->at=now;r->pcm=(unsigned)a->decoder.pcm_size;r->audio_es=(unsigned)a->decoder.input_size;
    r->video_es=(unsigned)xf_mpeg1_stream_used();r->ps=(unsigned)m->ps.count;r->video_queue=m->player->count+m->player->pending;
    r->http_age=now-m->stream.progress;r->received=m->ps.received;r->service_gap_us=m->last_service_gap_us;
    r->submit_age_us=m->last_submit_at?us-m->last_submit_at:0;
    if(m->service_at && us-m->service_at>r->service_gap_us)r->service_gap_us=us-m->service_at;
    if(r->service_gap_us>=170667)reason="CPU service gap exceeded 170.7 ms DMA cover";
    else if(m->ps.payload<m->ps.payload_end && !m->ps.payload_kind && !xf_mpeg1_stream_space())reason="Demux blocked on full video ES";
    else if(m->ps.payload<m->ps.payload_end && m->ps.payload_kind && !xf_audio_space(&a->decoder))reason="Demux blocked on full audio ES";
    else if(!r->pcm && !r->audio_es && !r->ps)reason="Network/compressed input empty";
    else if(r->pcm<16384 && r->audio_es)reason="PCM low with compressed audio queued";
    else if(r->pcm<16384)reason="PCM low; awaiting demuxed audio";
    else if(!r->video_queue)reason="PCM available; awaiting decoded video";
    else reason="DMA drained with PCM/video available";
    snprintf(r->reason,sizeof(r->reason),"%s",reason);m->rebuffer_open=1;
}
static void check_drain(xf_jvideo *m,uint32_t now) {
    xf_av_audio *a=&m->audio;
    if(a->started && !a->queued && !m->pause_requested && !a->decoder.finished && !a->rebuffering && !a->bridge) {
        rebuffer_event(m,now);a->rebuffering=1;++a->underruns;transition(m,XF_AV_REBUFFERING,now);
    }
}
static void output(xf_jvideo *m,int ready,uint32_t now) {
    uint64_t before=m->backend.microseconds(m->backend.context),submitted=m->audio.submitted;unsigned underruns=m->audio.underruns;
    {uint64_t position;xf_av_audio_clock(&m->audio,&position);check_drain(m,now);}
    /* A transient empty picture queue between codec calls is not network
     * starvation. Keep steady DMA fed; only actual input buffering holds it.
     * The separate ready argument still gates initial/recovery preroll. */
    xf_av_audio_output(&m->audio,ready || !m->player->buffering,ready,m->player->state!=XF_VIDEO_ENDED && m->mode!=XF_AV_AUDIO_ONLY);
    if(m->audio.underruns!=underruns)rebuffer_event(m,now);
    if(m->audio.submitted!=submitted)m->last_submit_at=m->backend.microseconds(m->backend.context);
    xf_time_add(&m->dma_times,m->backend.microseconds(m->backend.context)-before);
}
/* High-priority cooperative audio service: no HTTP, video decode or GPU waits.
 * Call on both sides of the non-preemptible video codec and before sleeping.
 * Decode only to the software low-water mark, four MP3 frames maximum per call.
 * A 32 KiB DMA window covers measured 103ms service stalls; all allocated rings retain their existing capacities. */
void xf_jvideo_service(xf_jvideo *m,uint32_t now) {
    xf_video *v=m->player;xf_av_audio *a=&m->audio;uint64_t before,position,started;unsigned i;
    int ready;
    if(m->state!=XF_JV_STREAMING)return;
    if(!a->enabled) {
        if(v->state==XF_VIDEO_PLAYING && !!v->paused!=!!m->pause_requested)xf_video_pause(v,now);
        transition(m,m->pause_requested?XF_AV_PAUSED:!m->stream.response.headers_done?XF_AV_BUFFERING:v->buffering && v->clock_started?XF_AV_REBUFFERING:v->clock_started?XF_AV_PLAYING:XF_AV_PREROLL,now);
        snprintf(m->wait_reason,sizeof(m->wait_reason),"%s",waiting(m));return;
    }
    started=m->backend.microseconds(m->backend.context);
    m->last_service_gap_us=m->service_at?started-m->service_at:0;
    if(m->service_at && started-m->service_at>m->max_service_gap_us)m->max_service_gap_us=started-m->service_at;
    m->service_at=started;++m->service_calls;
    before=m->backend.microseconds(m->backend.context);xf_av_audio_clock(a,&position);
    xf_time_add(&m->clock_times,m->backend.microseconds(m->backend.context)-before);
    /* Once the last picture is decoded, cap generated tail/bridge silence
     * at the known video end (one final descriptor may be padded). Never
     * truncate actual decoded PCM, including codec padding. */
    if(v->eof && v->info.fps>0) {
        int64_t end=(int64_t)(v->last_decoded_pts+(uint64_t)(1000000.0/v->info.fps+0.5))-a->clock_offset_us;
        if(end>0)a->tail_limit_frames=((uint64_t)end*48000+999999)/1000000;
    }
    /* Recovery needs ONE usable picture, not a completely full queue.
     * A held master clock explicitly permits decode-ahead in video.c. */
    ready=m->mode==XF_AV_AUDIO_ONLY || v->pending || v->count || v->state==XF_VIDEO_ENDED;
    if(m->pause_requested && !a->queued && !v->paused) {
        if(v->state==XF_VIDEO_PLAYING)xf_video_pause(v,now);
        a->rebuffering=1;
    }
    if(!m->pause_requested && v->paused)xf_video_pause(v,now);
    check_drain(m,now);
    if(!m->pause_requested)output(m,ready,now);
    if(!m->pause_requested || a->queued)for(i=0;i<4 && !a->failed;++i) {
        unsigned frames=a->decoder.mp3_frames;
        if(a->decoder.pcm_size>=XF_AV_PCM_TARGET)break;
        before=m->backend.microseconds(m->backend.context);xf_av_audio_decode(a,m->decoder_end);
        if(a->decoder.mp3_frames==frames)break;
        xf_time_add(&m->audio_times,m->backend.microseconds(m->backend.context)-before);
        if(!m->pause_requested)output(m,ready,now);
        if(m->backend.microseconds(m->backend.context)-started>=6000)break;
    }
    /* Retain the existing explicit, counted EOF/gap bridge only for actual
     * full-video-ES multiplex head-of-line blockage. Never free-run a clock. */
    if(!m->pause_requested && a->decoder.pcm_size<16384 && a->decoder.input_size<2048 &&
       !a->decoder.finished && a->have_video_pts && m->ps.payload<m->ps.payload_end &&
       !m->ps.payload_kind && !m->ps.discard_video && !xf_mpeg1_stream_space()) {
        if(!a->bridge)++a->gap_events;
        a->bridge=1;
    }
    if(!m->pause_requested)output(m,ready,now);
    xf_av_audio_clock(a,&position);
    if(m->mode==XF_AV_AUDIO_ONLY || v->state==XF_VIDEO_ENDED)v->timeline_us=position;
    if(m->mode==XF_AV_AUDIO_ONLY)v->paused=m->pause_requested && !a->queued;
    if(m->rebuffer_open && !a->rebuffering) {
        m->rebuffers[(m->rebuffer_count-1)%XF_AV_REBUFFERS].duration=now-m->rebuffers[(m->rebuffer_count-1)%XF_AV_REBUFFERS].at;
        m->rebuffer_open=0;
    }
    m->submit_calls=(unsigned)(a->submitted/512);m->last_pcm=a->decoder.pcm_size;m->last_dma=a->queued;
    xf_time_add(&m->refill_times,m->backend.microseconds(m->backend.context)-started);
    if(a->failed){snprintf(m->error_origin,sizeof(m->error_origin),"audio/DMA/PTS");finish(m,1,a->error,now);return;}
    transition(m,m->pause_requested && !a->queued?XF_AV_PAUSED:!m->stream.response.headers_done?XF_AV_BUFFERING:!a->started?XF_AV_PREROLL:
        a->rebuffering?XF_AV_REBUFFERING:XF_AV_PLAYING,now);
    snprintf(m->wait_reason,sizeof(m->wait_reason),"%s",waiting(m));
}
int xf_jvideo_export(const xf_jvideo *m,const char *path) {
    FILE *f=fopen(path,"w");unsigned k,i,n;if(!f)return 0;
    fprintf(f,"# XenonFin 0.7.6 mode=%u state=%s origin=%s reason=%s\n",m->mode,xf_jvideo_state_name(m->av_state),m->error_origin,m->wait_reason);
    fprintf(f,"# HTTP status=%u code=%d done=%d complete=%d failed=%d active=%d rx=%llu PS=%llu backpressure=%d\n",
        m->stream_status,m->terminal_http_code,m->terminal_http_done,m->terminal_http_complete,m->terminal_http_failed,m->stream.active,
        (unsigned long long)m->stream.received_size,(unsigned long long)m->ps.received,m->stream.backpressure);
    fprintf(f,"# PES V=%llu A=%llu lastPTS V=%lld A=%lld PCM=%u DMA=%u submitted=%llu consumed=%llu drift_us=%lld max_refill_gap_us=%llu\n",
        (unsigned long long)m->ps.packets,(unsigned long long)m->ps.audio_packets,(long long)m->ps.last_video_pts,(long long)m->ps.last_audio_pts,
        (unsigned)m->last_pcm,(unsigned)m->last_dma,(unsigned long long)m->terminal_submitted,(unsigned long long)m->terminal_played,
        (long long)m->terminal_video_pts-(long long)m->terminal_audio_us,(unsigned long long)m->max_service_gap_us);
    fprintf(f,"# after_cleanup_consumed=%llu submitted=%llu\n",(unsigned long long)m->audio.played,(unsigned long long)m->audio.submitted);
    fprintf(f,"# drain caller=%s:%u from=%u av_from=%s at_ms=%u failed=%u origin=%s first=%s\n",
        m->drain_caller,m->drain_line,m->drain_from,xf_jvideo_state_name(m->drain_av_from),m->drain_at,m->drain_failed,m->drain_origin,m->drain_message);
    fprintf(f,"# drain queue_first=%d queue_last=%d cached=%u valid=%d submitted_at_entry=%llu played_at_entry=%llu timeout=%u secondary=%s\n",
        m->drain_first_queue,m->drain_last_queue,m->audio.queued,m->audio.queue_valid,(unsigned long long)m->drain_submitted,(unsigned long long)m->drain_played,m->drain_timed_out,m->drain_error);
    fprintf(f,"# last_read=%d receive_calls=%u remote_eof=%u HTTP_error=%s app_error=%s\n",m->stream.last_receive_result,m->stream.receive_calls,m->stream.eof_seen,m->terminal_http_error,m->message);
    fprintf(f,"# TCP wnd=%u announced=%u refused=%u blocked_bytes=%u flags=%x state=%u idle_ms=%u window_updates=%u\n",
        m->stream.receive_window,m->stream.announced_window,m->stream.refused_bytes,m->stream.blocked_bytes,
        m->stream.tcp_flags,m->stream.tcp_state,m->stream.idle_age,m->stream.window_updates);
    fprintf(f,"# DMA raw=%08x native=%u rptr=%u last_valid=%u remaining=%u wraps=%u\n",m->terminal_dma_register,m->dma_seen,m->terminal_dma_register&31,(m->terminal_dma_register>>8)&31,m->terminal_dma_register>>16,m->dma_wraps);
    fprintf(f,"# MP3 max_us=%llu DMA-submit max_us=%llu clock-read max_us=%llu\n",(unsigned long long)m->audio_times.max,(unsigned long long)m->dma_times.max,(unsigned long long)m->clock_times.max);
    fprintf(f,"# audio_overruns=%u counter_faults=%u MP3_errors=%u bad_sync_bytes=%u\n",m->dma_overruns,m->dma_counter_faults,m->audio.decoder_errors,m->audio.decoder.bad_bytes);
    fprintf(f,"stage,value,last_progress_ms\n");
    for(i=0;i<XF_AV_STAGE_COUNT;++i)fprintf(f,"%u,%llu,%u\n",i,(unsigned long long)m->stages[i].value,m->stages[i].changed_at);
    fprintf(f,"kind,index,elementary_offset,pts_90k\n");
    for(k=0;k<2;++k)for(i=0;i<m->pts_count[k];++i)fprintf(f,"%c,%u,%llu,%lld\n",k?'A':'V',i,(unsigned long long)m->first_pts[k][i].offset,(long long)m->first_pts[k][i].pts);
    fprintf(f,"# rebuffer_events=%u open=%u\n",m->rebuffer_count,m->rebuffer_open);
    fprintf(f,"rebuffer_at_ms,duration_ms,reason,PCM,audio_ES,video_ES,PS,video_queue,service_gap_us,submit_age_us,HTTP_age_ms,received_bytes\n");
    n=m->rebuffer_count>XF_AV_REBUFFERS?XF_AV_REBUFFERS:m->rebuffer_count;
    for(i=m->rebuffer_count-n;i<m->rebuffer_count;++i) {
        const xf_av_rebuffer *r=&m->rebuffers[i%XF_AV_REBUFFERS];
        fprintf(f,"%u,%u,%s,%u,%u,%u,%u,%u,%llu,%llu,%u,%llu\n",r->at,r->duration,r->reason,r->pcm,r->audio_es,r->video_es,r->ps,r->video_queue,(unsigned long long)r->service_gap_us,(unsigned long long)r->submit_age_us,r->http_age,(unsigned long long)r->received);
    }
    fprintf(f,"state,time_ms\n");n=m->transition_count>32?32:m->transition_count;
    for(i=m->transition_count-n;i<m->transition_count;++i)fprintf(f,"%s,%u\n",xf_jvideo_state_name(m->transitions[i%32].state),m->transitions[i%32].at);
    {int ok=!ferror(f);if(fclose(f))ok=0;return ok;}
}
void xf_jvideo_stop(xf_jvideo *m,uint32_t now) {
    if(xf_jvideo_active(m) && m->state!=XF_JV_STOPPING){m->stopped=1;finish(m,0,"Video stopped; reporting and stopping server encoding.",now);}
}
void xf_jvideo_pause(xf_jvideo *m,uint32_t now) {
    if(!xf_jvideo_active(m) || m->state==XF_JV_STOPPING)return;
    m->pause_requested=!m->pause_requested;m->report_due=1;
    if(!m->pause_requested && m->player->paused)xf_video_pause(m->player,now);
}
int xf_jvideo_play(xf_jvideo *m,const xf_endpoint *ep,const char *device,const char *token,const char *user,const xf_item *item,const xf_video_backend *backend,uint32_t now) {
    char path[160],body[1800];
    if(xf_jvideo_active(m) || (strcmp(item->type,"Movie") && strcmp(item->type,"Episode")) || !xf_music_id(item->id) ||
       !xf_music_id(device) || !xf_music_id(user) || !token || !*token || strlen(token)>=sizeof(m->token))return 0;
    xf_jvideo_forget(m);xf_video_stop(m->player);m->endpoint=*ep;m->backend=*backend;
    snprintf(m->device,sizeof(m->device),"%s",device);snprintf(m->token,sizeof(m->token),"%s",token);
    snprintf(m->user,sizeof(m->user),"%s",user);snprintf(m->item,sizeof(m->item),"%s",item->id);snprintf(m->title,sizeof(m->title),"%.180s",item->name);
    if(!xf_video_profile(body,sizeof(body),user))return 0;
    if(m->lease || (m->mode!=XF_AV_VIDEO_ONLY && (!m->audio.backend.queued || !xf_audio_acquire(&m->audio.backend)))){snprintf(m->message,sizeof(m->message),"Audio device is starting, faulty or owned; inspect Audio diagnostics.");return 0;}
    m->lease=m->mode!=XF_AV_VIDEO_ONLY;
    snprintf(path,sizeof(path),"/Items/%s/PlaybackInfo",item->id);control(m,JV_NEGOTIATE,"POST",path,body,now);
    {unsigned i;for(i=0;i<XF_AV_STAGE_COUNT;++i)m->stages[i].changed_at=now;}
    transition(m,XF_AV_BUFFERING,now);m->state=XF_JV_NEGOTIATING;m->rate_at=m->progress_at=now;snprintf(m->message,sizeof(m->message),"Negotiating MPEG-1 / MPEG-PS transcoding with MP3 audio.");return 1;
}
static void report(xf_jvideo *m,unsigned kind,uint32_t now) {
    xf_music_source source;char body[700];const char *path=kind==JV_START?"/Sessions/Playing":kind==JV_STOP?"/Sessions/Playing/Stopped":"/Sessions/Playing/Progress";
    memset(&source,0,sizeof(source));snprintf(source.source_id,sizeof(source.source_id),"%s",m->source.source_id);snprintf(source.session_id,sizeof(source.session_id),"%s",m->source.session_id);source.transcode=1;
    uint64_t position=m->player->timeline_us;
    if(m->audio.enabled)xf_av_audio_clock(&m->audio,&position);
    if(!xf_music_report(body,sizeof(body),m->item,&source,position*10,m->pause_requested || m->player->paused || m->player->buffering || m->audio.rebuffering)) {
        snprintf(m->report_message,sizeof(m->report_message),"Cannot construct video playback report.");return;
    }
    control(m,kind,"POST",path,body,now);m->report_at=now;m->report_due=0;
    if(kind==JV_START)m->sent_start=1;
    if(kind==JV_STOP)m->sent_stop=1;
}
static void handle(xf_jvideo *m,uint32_t now) {
    xf_response *r=&m->control.response;unsigned kind=m->control_kind;
    m->control_pending=0;m->report_status=(unsigned)r->status;
    if(kind==JV_START)m->start_status=(unsigned)r->status;
    if(kind==JV_PROGRESS){m->progress_status=(unsigned)r->status;++m->progress_reports;}
    if(kind==JV_STOP)m->stop_status=(unsigned)r->status;
    if(kind==JV_CLEANUP)m->cleanup_status=(unsigned)r->status;
    if(r->status==401)m->session_expired=1;
    if(m->control.error[0] || !r->complete || r->failed || r->status<200 || r->status>=300) {
        if(kind==JV_NEGOTIATE) {
            char text[180];snprintf(m->error_origin,sizeof(m->error_origin),"PlaybackInfo HTTP");snprintf(text,sizeof(text),"PlaybackInfo HTTP %d: %.90s",r->status,m->control.error[0]?m->control.error:"server denied playback");finish(m,1,text,now);
        } else {++m->report_errors;snprintf(m->report_message,sizeof(m->report_message),"Video report/cleanup HTTP %d.",r->status);}
    } else if(kind==JV_NEGOTIATE) {
        if(!xf_video_source_parse(r,m->item,m->device,&m->source,m->message,sizeof(m->message))){snprintf(m->error_origin,sizeof(m->error_origin),"PlaybackInfo format");finish(m,1,NULL,now);}
        else if(!xf_video_start_network(m->player,&m->backend))finish(m,1,m->player->message,now);
        else {
            if(m->mode==XF_AV_AUDIO_ONLY && !m->source.has_audio) {
                finish(m,1,"Mode B requires an audio-bearing source.",now);xf_wipe(r,sizeof(*r));return;
            }
            m->attached=1;xf_video_ps_init(&m->ps);
            xf_video_ps_timestamps(&m->ps,stamp,m);m->ps.optional_initial_pts=1;
            if(m->source.has_audio && m->mode!=XF_AV_VIDEO_ONLY) {
                if(!m->audio.backend.queued || !m->audio.backend.available || !m->audio.backend.submit) {
                    finish(m,1,"No movie audio output backend configured.",now);xf_wipe(r,sizeof(*r));return;
                }
                m->audio.enabled=1;
                xf_video_ps_audio(&m->ps,xf_av_audio_byte,xf_av_audio_space,&m->audio);
                m->ps.optional_initial_pts=0;
                xf_video_master(m->player,xf_av_audio_clock,&m->audio);
                m->ps.discard_video=m->mode==XF_AV_AUDIO_ONLY;
            }
            xf_http_video(&m->stream,&m->endpoint,m->device,m->token,m->source.path,xf_video_ps_byte,xf_video_ps_space,&m->ps,now);
            m->state=XF_JV_STREAMING;snprintf(m->message,sizeof(m->message),"HTTP MPEG-PS; MPEG-1 + MP3 requested, confirming audio frames.");
        }
    } else m->report_message[0]=0;
    xf_wipe(r,sizeof(*r));
}
void xf_jvideo_tick(xf_jvideo *m,uint32_t now,int ready) {
    uint64_t before,cost;
    if(!xf_jvideo_active(m)){if(m->lease && m->audio.backend.queued(m->audio.backend.context)==0 && xf_audio_release(&m->audio.backend))m->lease=0;return;}
    if(m->audio.enabled) {uint64_t position;xf_av_audio_clock(&m->audio,&position);}
    before=m->backend.microseconds(m->backend.context);
    xf_http_tick(&m->control,now,ready);xf_http_tick(&m->stream,now,ready);
    cost=m->backend.microseconds(m->backend.context)-before;m->network_us+=cost;xf_time_add(&m->network_times,cost);
    if(m->control_pending && m->control.done && !m->control.active)handle(m,now);
    if(m->session_expired && m->state!=XF_JV_STOPPING)finish(m,1,"HTTP 401: video session expired. Sign in again.",now);
    if(m->state==XF_JV_STREAMING) {
        if(m->stream.response.headers_done) {
            const char *type=m->stream.response.content_type;m->stream_status=(unsigned)m->stream.response.status;
            if(m->stream_status==401)m->session_expired=1;
            if(m->stream_status!=200 || (*type && strncmp(type,"video/mpeg",10) && strncmp(type,"application/octet-stream",24))) {
                char text[128];snprintf(m->error_origin,sizeof(m->error_origin),"HTTP status/type");snprintf(text,sizeof(text),"Video stream HTTP %u / unsupported Content-Type.",m->stream_status);finish(m,1,text,now);
            }
        }
        if(m->state==XF_JV_STREAMING && m->stream.done && !m->stream.active && m->stream.error[0]){snprintf(m->error_origin,sizeof(m->error_origin),"HTTP transport");finish(m,1,m->stream.error,now);}
        if(m->state==XF_JV_STREAMING && !m->decoder_end && m->player->state!=XF_VIDEO_ERROR && m->player->state!=XF_VIDEO_ENDED) {
            int eof=m->stream.done && !m->stream.active;
            before=m->backend.microseconds(m->backend.context);
            if(xf_video_ps_tick(&m->ps,xf_mpeg1_stream_byte,xf_mpeg1_stream_space,eof)<0) {
                snprintf(m->error_origin,sizeof(m->error_origin),"MPEG-PS demux");finish(m,1,m->audio.failed?m->audio.error:xf_mpeg1_stream_error()[0]?xf_mpeg1_stream_error():m->ps.error,now);
            } else if(eof && !m->ps.count && m->ps.payload==m->ps.payload_end) {
                if(m->mode!=XF_AV_AUDIO_ONLY && !xf_mpeg1_stream_end())finish(m,1,xf_mpeg1_stream_error(),now);
                m->decoder_end=1;
            }
            cost=m->backend.microseconds(m->backend.context)-before;m->demux_us+=cost;xf_time_add(&m->demux_times,cost);
        }
        xf_jvideo_service(m,now);
        if(m->state==XF_JV_STREAMING) {
            uint64_t values[XF_AV_STAGE_COUNT]={m->stream.received_size,m->ps.video_bytes+m->ps.audio_bytes,m->ps.audio_bytes,
                m->audio.decoder.mp3_frames,m->audio.decoder.output_frames,m->audio.submitted,m->audio.played,
                m->player->decoded,m->player->timeline_us,m->player->rendered};unsigned i;
            for(i=0;i<XF_AV_STAGE_COUNT;++i)if(values[i]!=m->stages[i].value){m->stages[i].value=values[i];m->stages[i].changed_at=now;}
            uint64_t progress=m->player->decoded+m->audio.decoder.mp3_frames+m->audio.played+m->ps.audio_bytes+m->ps.video_bytes;
            if(progress!=m->progress_value || m->pause_requested){m->progress_value=progress;m->progress_at=now;m->stalled_ms=0;}
            else m->stalled_ms=(uint32_t)(now-m->progress_at);
            if(m->audio.enabled && !m->audio.started && !m->pause_requested && m->stalled_ms>=20000) {
                snprintf(m->error_origin,sizeof(m->error_origin),"initial audio preroll");
                finish(m,1,"No initial MP3/PTS/preroll progress for 20s; inspect server audio transcode.",now);
            }
            /* A snapshot is diagnostic, not a fabricated network failure.
             * Protocol/decoder/DMA faults retain their own terminal errors. */
        }
        if(m->state==XF_JV_STREAMING && m->player->state==XF_VIDEO_ERROR){snprintf(m->error_origin,sizeof(m->error_origin),"video/decode/GPU");finish(m,1,m->player->message,now);}
        if(m->state==XF_JV_STREAMING && (m->player->state==XF_VIDEO_ENDED || (m->mode==XF_AV_AUDIO_ONLY && m->decoder_end)) && xf_av_audio_done(&m->audio)) {
            int short_stream=m->source.runtime>0 && m->player->timeline_us*10+30000000<m->source.runtime;
            finish(m,short_stream,short_stream?"Stream ended earlier than source runtime; inspect server encoder/logs.":"Video ended; stopping server encoding.",now);
        }
        if((uint32_t)(now-m->rate_at)>=1000) {
            m->kbps=(unsigned)((m->ps.received-m->rate_bytes)*8/(uint32_t)(now-m->rate_at));m->rate_bytes=m->ps.received;m->rate_at=now;
        }
    }
    if(!m->control_pending && !m->control.active) {
        if(m->attached && (m->player->rendered || (m->mode==XF_AV_AUDIO_ONLY && m->audio.started)) && m->source.session_id[0] && !m->sent_start)report(m,JV_START,now);
        else if(m->state==XF_JV_STOPPING) {
            m->drain_last_queue=m->audio.queue_reading;
            if(m->audio.enabled && (!m->audio.queue_valid || m->audio.queued)) {
                if((uint32_t)(now-m->drain_at)<2000)return;
                m->drain_timed_out=1;
                snprintf(m->drain_error,sizeof(m->drain_error),"%s",m->audio.queue_valid?
                    "Audio DMA remained nonempty for 2s; ownership retained.":
                    "Audio DMA drain unverified for 2s: invalid queue reading; ownership retained.");
                /* A failed read is not a measured occupied queue. Preserve the
                 * error that entered STOPPING instead of masking it here. */
                if(!m->failed){snprintf(m->error_origin,sizeof(m->error_origin),"DMA stop drain");snprintf(m->message,sizeof(m->message),"%s",m->drain_error);}
                m->failed=1;transition(m,XF_AV_ERROR,now);
            }
            if(m->sent_start && !m->sent_stop) {report(m,JV_STOP,now);return;}
            if(m->source.session_id[0] && !m->cleaned) {
                char path[240];m->cleaned=1;snprintf(path,sizeof(path),"/Videos/ActiveEncodings?DeviceId=%s&PlaySessionId=%s",m->device,m->source.session_id);
                control(m,JV_CLEANUP,"DELETE",path,NULL,now);return;
            }
            if(m->lease && m->audio.backend.queued(m->audio.backend.context)==0 && xf_audio_release(&m->audio.backend))m->lease=0;
            m->state=m->failed?XF_JV_ERROR:XF_JV_FINISHED;xf_wipe(m->token,sizeof(m->token));
        } else if(m->sent_start && (m->report_due || (uint32_t)(now-m->report_at)>=5000))report(m,JV_PROGRESS,now);
    }
}
