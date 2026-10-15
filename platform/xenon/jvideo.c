#include "jvideo.h"
#include "music_api.h"
#include <stdio.h>
#include <string.h>
enum { JV_NEGOTIATE=1,JV_START,JV_PROGRESS,JV_STOP,JV_CLEANUP };
void xf_jvideo_init(xf_jvideo *m,xf_video *v) {memset(m,0,sizeof(*m));m->player=v;xf_http_init(&m->stream);xf_http_init(&m->control);}
int xf_jvideo_active(const xf_jvideo *m) {return m->state>=XF_JV_NEGOTIATING && m->state<=XF_JV_STOPPING;}
void xf_jvideo_forget(xf_jvideo *m) {
    xf_video *v=m->player;int owned=xf_jvideo_active(m);
    xf_http_cancel(&m->stream);xf_http_cancel(&m->control);if(owned)xf_video_stop(v);
    xf_wipe(m,sizeof(*m));xf_jvideo_init(m,v);
}
static void control(xf_jvideo *m,unsigned kind,const char *method,const char *path,const char *body,uint32_t now) {
    xf_http_api(&m->control,&m->endpoint,m->device,m->token,method,path,body,now);m->control_kind=kind;m->control_pending=1;
}
static void finish(xf_jvideo *m,int failed,const char *message,uint32_t now) {
    (void)now;
    if(message)snprintf(m->message,sizeof(m->message),"%s",message);
    m->failed|=failed;xf_http_cancel(&m->stream);
    if(m->control_pending && (m->control_kind==JV_NEGOTIATE || m->control_kind==JV_PROGRESS)) {
        xf_http_cancel(&m->control);m->control_pending=0;xf_wipe(&m->control.response,sizeof(m->control.response));
    }
    if(m->player->state!=XF_VIDEO_ENDED && m->player->state!=XF_VIDEO_ERROR)xf_video_stop(m->player);
    m->state=XF_JV_STOPPING;
}
void xf_jvideo_stop(xf_jvideo *m,uint32_t now) {
    if(xf_jvideo_active(m))finish(m,0,"Video stopped; reporting and stopping server encoding.",now);
}
int xf_jvideo_play(xf_jvideo *m,const xf_endpoint *ep,const char *device,const char *token,const char *user,const xf_item *item,const xf_video_backend *backend,uint32_t now) {
    char path[160],body[1800];
    if(xf_jvideo_active(m) || (strcmp(item->type,"Movie") && strcmp(item->type,"Episode")) || !xf_music_id(item->id) ||
       !xf_music_id(device) || !xf_music_id(user) || !token || !*token || strlen(token)>=sizeof(m->token))return 0;
    xf_jvideo_forget(m);xf_video_stop(m->player);m->endpoint=*ep;m->backend=*backend;
    snprintf(m->device,sizeof(m->device),"%s",device);snprintf(m->token,sizeof(m->token),"%s",token);
    snprintf(m->user,sizeof(m->user),"%s",user);snprintf(m->item,sizeof(m->item),"%s",item->id);snprintf(m->title,sizeof(m->title),"%.180s",item->name);
    if(!xf_video_profile(body,sizeof(body),user))return 0;
    snprintf(path,sizeof(path),"/Items/%s/PlaybackInfo",item->id);control(m,JV_NEGOTIATE,"POST",path,body,now);
    m->state=XF_JV_NEGOTIATING;m->rate_at=now;snprintf(m->message,sizeof(m->message),"Negotiating MPEG-1 / MPEG-PS transcoding (silent playback).");return 1;
}
static void report(xf_jvideo *m,unsigned kind,uint32_t now) {
    xf_music_source source;char body[700];const char *path=kind==JV_START?"/Sessions/Playing":kind==JV_STOP?"/Sessions/Playing/Stopped":"/Sessions/Playing/Progress";
    memset(&source,0,sizeof(source));snprintf(source.source_id,sizeof(source.source_id),"%s",m->source.source_id);snprintf(source.session_id,sizeof(source.session_id),"%s",m->source.session_id);source.transcode=1;
    if(!xf_music_report(body,sizeof(body),m->item,&source,m->player->timeline_us*10,m->player->paused || m->player->buffering)) {
        snprintf(m->report_message,sizeof(m->report_message),"Cannot construct video playback report.");return;
    }
    control(m,kind,"POST",path,body,now);m->report_at=now;
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
            char text[180];snprintf(text,sizeof(text),"PlaybackInfo HTTP %d: %.90s",r->status,m->control.error[0]?m->control.error:"server denied playback");finish(m,1,text,now);
        } else {++m->report_errors;snprintf(m->report_message,sizeof(m->report_message),"Video report/cleanup HTTP %d.",r->status);}
    } else if(kind==JV_NEGOTIATE) {
        if(!xf_video_source_parse(r,m->item,m->device,&m->source,m->message,sizeof(m->message)))finish(m,1,NULL,now);
        else if(!xf_video_start_network(m->player,&m->backend))finish(m,1,m->player->message,now);
        else {
            m->attached=1;xf_video_ps_init(&m->ps);
            xf_http_video(&m->stream,&m->endpoint,m->device,m->token,m->source.path,xf_video_ps_byte,xf_video_ps_space,&m->ps,now);
            m->state=XF_JV_STREAMING;snprintf(m->message,sizeof(m->message),"HTTP progressive MPEG-PS -> MPEG-1; audio packets discarded.");
        }
    } else m->report_message[0]=0;
    xf_wipe(r,sizeof(*r));
}
void xf_jvideo_tick(xf_jvideo *m,uint32_t now,int ready) {
    uint64_t before,cost;
    if(!xf_jvideo_active(m))return;
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
                char text[128];snprintf(text,sizeof(text),"Video stream HTTP %u / unsupported Content-Type.",m->stream_status);finish(m,1,text,now);
            }
        }
        if(m->state==XF_JV_STREAMING && m->stream.done && !m->stream.active && m->stream.error[0])finish(m,1,m->stream.error,now);
        if(m->state==XF_JV_STREAMING && !m->decoder_end && m->player->state!=XF_VIDEO_ERROR && m->player->state!=XF_VIDEO_ENDED) {
            int eof=m->stream.done && !m->stream.active;
            before=m->backend.microseconds(m->backend.context);
            if(xf_video_ps_tick(&m->ps,xf_mpeg1_stream_byte,xf_mpeg1_stream_space,eof)<0)
                finish(m,1,xf_mpeg1_stream_error()[0]?xf_mpeg1_stream_error():m->ps.error,now);
            else if(eof && !m->ps.count && m->ps.payload==m->ps.payload_end) {
                if(!xf_mpeg1_stream_end())finish(m,1,xf_mpeg1_stream_error(),now);
                m->decoder_end=1;
            }
            cost=m->backend.microseconds(m->backend.context)-before;m->demux_us+=cost;xf_time_add(&m->demux_times,cost);
        }
        if(m->state==XF_JV_STREAMING && m->player->state==XF_VIDEO_ERROR)finish(m,1,m->player->message,now);
        if(m->state==XF_JV_STREAMING && m->player->state==XF_VIDEO_ENDED) {
            int short_stream=m->source.runtime>0 && m->player->timeline_us*10+30000000<m->source.runtime;
            finish(m,short_stream,short_stream?"Stream ended earlier than source runtime; inspect server encoder/logs.":"Video ended; stopping server encoding.",now);
        }
        if((uint32_t)(now-m->rate_at)>=1000) {
            m->kbps=(unsigned)((m->ps.received-m->rate_bytes)*8/(uint32_t)(now-m->rate_at));m->rate_bytes=m->ps.received;m->rate_at=now;
        }
    }
    if(!m->control_pending && !m->control.active) {
        if(m->attached && m->player->rendered && m->source.session_id[0] && !m->sent_start)report(m,JV_START,now);
        else if(m->state==XF_JV_STOPPING) {
            if(m->sent_start && !m->sent_stop) {report(m,JV_STOP,now);return;}
            if(m->source.session_id[0] && !m->cleaned) {
                char path[240];m->cleaned=1;snprintf(path,sizeof(path),"/Videos/ActiveEncodings?DeviceId=%s&PlaySessionId=%s",m->device,m->source.session_id);
                control(m,JV_CLEANUP,"DELETE",path,NULL,now);return;
            }
            m->state=m->failed?XF_JV_ERROR:XF_JV_FINISHED;xf_wipe(m->token,sizeof(m->token));
        } else if(m->sent_start && (uint32_t)(now-m->report_at)>=5000)report(m,JV_PROGRESS,now);
    }
}
