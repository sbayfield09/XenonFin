#include "video.h"
#include <stdlib.h>
#include <string.h>
static void release(xf_video *v) {
    if(v->file) fclose(v->file);
    v->file=NULL;
    v->codec->close(); free(v->bytes); v->bytes=NULL;
    if(v->backend_started) v->backend.end(v->backend.context);
    v->backend_started=0; v->count=v->pending=v->ready=0;
}
static void record(xf_video *v, uint64_t pts, uint64_t work, unsigned reason) {
    xf_video_trace *t=&v->trace[v->trace_count%XF_VIDEO_TRACE];
    uint64_t late=v->clock_started && v->timeline_us>pts?v->timeline_us-pts:0;
    uint64_t span=v->pending && pts==v->pending_pts?v->pending_span_us:work;
    *t=(xf_video_trace){pts,v->timeline_us,late,work,span,reason,v->count};
    ++v->trace_count; if(v->trace_count>XF_VIDEO_TRACE) ++v->trace_overwritten;
    xf_time_add(&v->processing,work);xf_time_add(&v->frame_span,span);
    if(reason<XF_DROP_REASONS) {
        ++v->dropped; ++v->drop_reason[reason]; xf_time_add(&v->drop_lateness,late);
    }
}
static void fail_reason(xf_video *v, const char *message, unsigned reason) {
    char saved[180]; snprintf(saved,sizeof(saved),"%s",message);
    if(v->pending) record(v,v->pending_pts,v->pending_work_us,reason);
    while(v->count) {
        xf_video_slot *s=&v->queue[v->head];record(v,s->pts,s->work_us,reason);
        --v->count;v->head=(v->head+1)%XF_VIDEO_QUEUE;
    }
    release(v); v->state=XF_VIDEO_ERROR; snprintf(v->message,sizeof(v->message),"%s",saved);
}
static void fail(xf_video *v, const char *message) { fail_reason(v,message,XF_DROP_DECODE); }
void xf_video_init(xf_video *v, const xf_video_codec *codec) {
    memset(v,0,sizeof(*v)); v->codec=codec; v->late_threshold_us=v->next_late_threshold_us=100000;
    snprintf(v->message,sizeof(v->message),"FAT USB: xenonfin-test.m1v. Up/Down: colour bars. LT late / RT VSync (stopped).");
}
void xf_video_stop(xf_video *v) {
    v->cancelled+=v->count+v->pending;release(v); v->state=XF_VIDEO_IDLE; v->paused=v->buffering=0;
    snprintf(v->message,sizeof(v->message),"Stopped. R-stick: save CSV; LT: lateness; RT: VSync; B: Libraries.");
}
int xf_video_options(xf_video *v, unsigned sync, uint64_t lateness_us) {
    if(xf_video_active(v) || sync>XF_SYNC_HALF || lateness_us>1000000) return 0;
    v->next_sync_mode=sync; v->next_late_threshold_us=lateness_us;return 1;
}
static void reset_for_start(xf_video *v, const xf_video_backend *backend) {
    const xf_video_codec *codec=v->codec; unsigned sync=v->next_sync_mode; uint64_t late=v->next_late_threshold_us;
    release(v); xf_video_init(v,codec);v->backend=*backend;v->sync.mode=v->next_sync_mode=sync;v->late_threshold_us=v->next_late_threshold_us=late;
}
int xf_video_active(const xf_video *v) { return v->state==XF_VIDEO_LOADING || v->state==XF_VIDEO_PLAYING; }
static int begin(xf_video *v) {
    if(!v->info.width || !v->info.height || v->info.width>XF_VIDEO_MAX_WIDTH || v->info.height>XF_VIDEO_MAX_HEIGHT ||
       ((v->info.width|v->info.height)&1) || !(v->info.fps>0 && v->info.fps<=30) || !(v->info.pixel_aspect>0 && v->info.pixel_aspect<10)) {
        fail(v,"Decoder returned an unsupported video format."); return 0;
    }
    if(!v->backend.present || !v->backend.begin(v->backend.context,&v->info,v->message,sizeof(v->message))) { fail(v,v->message); return 0; }
    v->backend_started=1; v->state=XF_VIDEO_PLAYING;
    v->last_clock_us=v->backend.microseconds(v->backend.context);
    snprintf(v->message,sizeof(v->message),"Silent %s video. A/Start pause; X stop; B Libraries.",v->streaming?"Jellyfin":"local"); return 1;
}
int xf_video_start(xf_video *v, const char *path, const xf_video_backend *backend) {
    long size;
    reset_for_start(v,backend);
    snprintf(v->path,sizeof(v->path),"%s",path);
    v->file=fopen(path,"rb");
    if(!v->file) { fail(v,"Cannot open test file; check FAT USB and xenonfin-test.m1v at its root."); return 0; }
    if(fseek(v->file,0,SEEK_END) || (size=ftell(v->file))<144 || size>(long)XF_VIDEO_MAX_FILE || fseek(v->file,0,SEEK_SET)) {
        fail(v,"File size/seek error; supported test clip is 144 bytes to 8 MiB."); return 0;
    }
    v->size=(size_t)size; v->bytes=malloc(v->size);
    if(!v->bytes) { fail(v,"Insufficient memory for bounded test clip."); return 0; }
    v->state=XF_VIDEO_LOADING; snprintf(v->message,sizeof(v->message),"Loading local clip in 32 KiB steps; B cancels."); return 1;
}
int xf_video_start_network(xf_video *v,const xf_video_backend *backend) {
    int rc;reset_for_start(v,backend);v->streaming=1;v->state=XF_VIDEO_LOADING;
    rc=v->codec->open(NULL,0,&v->info,v->message,sizeof(v->message));
    if(rc<0) {fail(v,v->message);return 0;}
    snprintf(v->message,sizeof(v->message),"Buffering authenticated MPEG-PS stream.");return 1;
}
int xf_video_start_colours(xf_video *v, const xf_video_backend *backend) {
    reset_for_start(v,backend); v->synthetic=1;
    v->info=(xf_video_info){320,240,30,1};
    if(!begin(v)) return 0;
    snprintf(v->message,sizeof(v->message),"Expected bars: black white RED GREEN BLUE cyan magenta yellow."); return 1;
}
static xf_video_image view(const xf_video *v, const xf_video_slot *slot) {
    xf_video_image image; unsigned n=v->info.width*v->info.height;
    image.width=v->info.width; image.height=v->info.height; image.y=slot->data;
    image.cb=slot->data+n; image.cr=slot->data+n+n/4;
    image.y_stride=image.width; image.c_stride=image.width/2; image.pts_us=slot->pts; return image;
}
/* Published BT.601 limited-range vectors, not framebuffer channel guesses.
 * Feed the SAME queue, converter/texture upload and display path as MPEG. */
static void colour_frame(xf_video *v, xf_video_slot *slot) {
    static const unsigned char values[8][3]={{16,128,128},{235,128,128},{81,90,240},{145,54,34},{41,240,110},{170,166,16},{106,202,222},{210,16,146}};
    unsigned x,y,w=v->info.width,h=v->info.height,n=w*h;
    for(y=0;y<h;++y) for(x=0;x<w;++x) slot->data[y*w+x]=values[x*8/w][0];
    for(y=0;y<h/2;++y) for(x=0;x<w/2;++x) {
        slot->data[n+y*(w/2)+x]=values[x*16/w][1];
        slot->data[n+n/4+y*(w/2)+x]=values[x*16/w][2];
    }
    slot->pts=0;
}
static uint64_t clock_update(xf_video *v) {
    uint64_t now=v->backend.microseconds(v->backend.context);
    if(now<v->last_clock_us) { ++v->clock_faults; now=v->last_clock_us; }
    v->last_clock_us=now;
    if(v->clock_started && !v->paused && !v->buffering) v->timeline_us=v->origin_pts+now-v->origin_us-v->paused_us;
    return now;
}
static void buffering(xf_video *v,int hold) {
    int was=v->paused || v->buffering;clock_update(v);
    if(hold && !v->buffering && v->clock_started) ++v->underruns;
    v->buffering=!!hold;
    if(!was && (v->paused || v->buffering))v->pause_at=v->last_clock_us;
    if(was && !v->paused && !v->buffering)v->paused_us+=v->last_clock_us-v->pause_at;
    v->sync.wait_at=0;
}
static int poll_pending(xf_video *v) {
    int rc;uint64_t start;
    if(!v->pending || v->ready) return 1;
    start=v->backend.microseconds(v->backend.context);
    rc=v->backend.poll(v->backend.context,v->message,sizeof(v->message));
    v->pending_work_us+=v->backend.microseconds(v->backend.context)-start;
    if(rc<0) { if(!v->pending_stall) ++v->gpu_stalls;v->pending_span_us=v->pending_source_work_us+v->backend.microseconds(v->backend.context)-v->prepare_at;clock_update(v);fail_reason(v,v->message,XF_DROP_GPU);return 0; }
    if(rc>0) {v->ready=1;v->pending_span_us=v->pending_source_work_us+v->backend.microseconds(v->backend.context)-v->prepare_at;}
    else if(!v->pending_stall && start-v->prepare_at>(uint64_t)(1000000.0/v->info.fps)) {++v->gpu_stalls;v->pending_stall=1;}
    return 1;
}
static int obsolete(const xf_video *v, uint64_t pts, uint64_t successor) {
    return v->clock_started && v->late_threshold_us && v->timeline_us>pts &&
        v->timeline_us-pts>v->late_threshold_us && successor<=v->timeline_us;
}
static int display_ready(xf_video *v) {
    uint64_t before,after,late;
    clock_update(v);
    if(!v->pending || !v->ready || v->paused) return 1;
    if(v->buffering) {if(!v->eof && v->count<XF_VIDEO_QUEUE)return 1;buffering(v,0);clock_update(v);}
    if(!v->clock_started) {
        if(!v->eof && v->count<XF_VIDEO_QUEUE) return 1;
    } else if(v->pending_pts>v->timeline_us) return 1;
    if(v->count && obsolete(v,v->pending_pts,v->queue[v->head].pts)) {
        record(v,v->pending_pts,v->pending_work_us,XF_DROP_LATE); v->pending=v->ready=0;
        if(v->sync.wait_at) {v->sync.wait_us+=v->last_clock_us+1-v->sync.wait_at;v->sync.wait_at=0;}
        return 1;
    }
    if(!xf_video_sync_ready(&v->sync,v->last_clock_us)) return 1;
    if(v->rendered && v->pending_pts<=v->last_present_pts) {++v->decode_failures;fail(v,"Unordered presentation timestamp.");return 0;}
    before=v->backend.microseconds(v->backend.context);
    if(!v->backend.present(v->backend.context,v->message,sizeof(v->message))) {fail_reason(v,v->message,XF_DROP_GPU);return 0;}
    after=v->backend.microseconds(v->backend.context); v->pending_work_us+=after-before;v->pending_span_us+=after-before;
    if(!v->clock_started) {
        v->clock_started=1;v->paused_us=0;v->origin_us=after;v->origin_pts=v->pending_pts;v->timeline_us=v->pending_pts;
        v->last_clock_us=after;v->fps_at=(uint32_t)(after/1000);
    } else clock_update(v);
    v->drift_us=(int64_t)v->timeline_us-(int64_t)v->pending_pts;
    late=v->timeline_us>v->pending_pts?v->timeline_us-v->pending_pts:0;
    xf_time_add(&v->lateness,late);
    if(v->rendered) xf_time_add(&v->intervals,after-v->last_present_at);
    record(v,v->pending_pts,v->pending_work_us,XF_DROP_REASONS);
    v->last_present_at=after;v->last_present_pts=v->pending_pts;
    ++v->rendered;v->pending=v->ready=0;return 1;
}
/* Presentation-only service. It never calls the decoder or waits for GPU,
 * timebase or VBlank. Main can service it on BOTH sides of controller/UI work. */
void xf_video_service(xf_video *v) {
    uint64_t before,after;
    if(v->state!=XF_VIDEO_PLAYING) return;
    before=clock_update(v);
    if(v->sync.mode && !v->sync.fallbacks) {
        if(v->backend.vblank) xf_video_sync_sample(&v->sync,before,v->backend.vblank(v->backend.context));
        else {++v->sync.faults;++v->sync.fallbacks;}
    }
    if(v->clock_started) {
        uint32_t ms=(uint32_t)(before/1000);
        if((uint32_t)(ms-v->fps_at)>=1000) {
            v->fps_milli=(unsigned)((uint64_t)(v->rendered-v->fps_frames)*1000000/(uint32_t)(ms-v->fps_at));
            v->fps_at=ms;v->fps_frames=v->rendered;
        }
    }
    if(!poll_pending(v) || !display_ready(v) || v->paused) return;
    if(!v->pending && v->count) {
        xf_video_slot *slot=&v->queue[v->head];
        while(v->count>1 && obsolete(v,slot->pts,v->queue[(v->head+1)%XF_VIDEO_QUEUE].pts)) {
            record(v,slot->pts,slot->work_us,XF_DROP_LATE);
            v->head=(v->head+1)%XF_VIDEO_QUEUE;--v->count;slot=&v->queue[v->head];
        }
        {
            xf_video_image image=view(v,slot);
            before=v->backend.microseconds(v->backend.context);
            if(!v->backend.submit(v->backend.context,&image,v->message,sizeof(v->message))) {fail_reason(v,v->message,XF_DROP_GPU);return;}
            after=v->backend.microseconds(v->backend.context);
            v->pending_source_work_us=slot->work_us;v->pending_span_us=slot->work_us+after-before;
            v->pending_work_us=slot->work_us+after-before;v->prepare_at=before;v->pending_stall=0;
            v->pending=1;v->ready=0;v->pending_pts=slot->pts;
            v->head=(v->head+1)%XF_VIDEO_QUEUE;--v->count;
            if(!poll_pending(v)) return;
        }
    }
    if(!display_ready(v)) return;
    if(v->streaming && v->input_wait && !v->count && !v->pending && !v->eof && !v->buffering) buffering(v,1);
    if(v->eof && !v->count && !v->pending) {
        v->codec->close();free(v->bytes);v->bytes=NULL;v->state=XF_VIDEO_ENDED;
        snprintf(v->message,sizeof(v->message),"%s",v->synthetic?"Bars: black white RED GREEN BLUE cyan magenta yellow.":"End. R-stick: save CSV. LT late / RT VSync; A/Y replay; B Libraries.");
    }
}
static int slack(xf_video *v, uint64_t budget) {
    clock_update(v);
    if(!v->clock_started || !v->pending || v->buffering) return 1;
    return v->pending_pts>v->timeline_us && v->pending_pts-v->timeline_us>budget;
}
int xf_video_ui_safe(xf_video *v) {
    if(v->state!=XF_VIDEO_PLAYING || v->paused) return 1;
    return slack(v,xf_time_p95(&v->ui_times)+2000);
}
void xf_video_tick(xf_video *v, uint32_t now) {
    uint64_t before,after; (void)now;
    if(v->state==XF_VIDEO_LOADING && v->streaming) {
        int rc=v->codec->open(NULL,0,&v->info,v->message,sizeof(v->message));
        if(rc<0) {++v->decode_failures;fail(v,v->message);return;}
        if(rc==1)begin(v);
        return;
    }
    if(v->state==XF_VIDEO_LOADING) {
        size_t count=v->size-v->loaded;if(count>32768) count=32768;
        if(fread(v->bytes+v->loaded,1,count,v->file)!=count) {fail(v,"USB/file read failed or clip was truncated while loading.");return;}
        v->loaded+=count;if(v->loaded!=v->size) return;
        fclose(v->file);v->file=NULL;
        if(!v->codec->open(v->bytes,v->size,&v->info,v->message,sizeof(v->message))) {++v->decode_failures;fail(v,v->message);return;}
        begin(v);return;
    }
    xf_video_service(v);
    if(v->state!=XF_VIDEO_PLAYING || v->paused || v->eof) return;
    if(v->count==XF_VIDEO_QUEUE) {++v->full_ticks;return;}
    /* Decoder stays cooperative; skip an expensive non-preemptible call
     * near a ready picture's deadline. Four slots already supply headroom. */
    if(!slack(v,v->decode_estimate_us+3000)) return;
    {
        xf_video_image source;int rc;
        xf_video_slot *slot=&v->queue[(v->head+v->count)%XF_VIDEO_QUEUE];
        if(v->clock_started && !v->count && !v->pending && v->timeline_us>v->last_present_pts+(uint64_t)(1000000.0/v->info.fps)) ++v->starved;
        before=v->backend.microseconds(v->backend.context);
        if(v->synthetic) {colour_frame(v,slot);rc=1;v->eof=1;++v->eof_events;}
        else rc=v->codec->next(&source,v->message,sizeof(v->message));
        after=v->backend.microseconds(v->backend.context)-before;
        if(rc<0) {++v->decode_failures;clock_update(v);fail(v,v->message);return;}
        v->input_wait=rc==2;
        if(rc==2) {if(!v->count && !v->pending && !v->buffering)buffering(v,1);return;}
        if(!rc) {v->eof=1;++v->eof_events;}
        else {
            slot->work_us=after;v->decode_us+=after;if(after>v->max_decode_us) v->max_decode_us=after;
            v->decode_estimate_us=v->decoded?(v->decode_estimate_us*7+after)/8:after;
            /* A single large I picture must not prevent ALL subsequent
             * decoding. Use a budget estimate, not the all-run maximum. */
            if(v->decode_estimate_us>20000) v->decode_estimate_us=20000;
            if(!v->synthetic) {
                unsigned row,width=v->info.width,height=v->info.height,n=width*height;
                if(source.width!=width || source.height!=height || source.y_stride<width || source.c_stride<width/2 ||
                   (v->decoded && source.pts_us<=v->last_decoded_pts)) {
                    ++v->decode_failures;fail(v,"Decoder picture dimensions/stride/timestamps changed.");return;
                }
                before=v->backend.microseconds(v->backend.context);
                for(row=0;row<height;++row) memcpy(slot->data+row*width,source.y+row*source.y_stride,width);
                for(row=0;row<height/2;++row) {
                    memcpy(slot->data+n+row*(width/2),source.cb+row*source.c_stride,width/2);
                    memcpy(slot->data+n+n/4+row*(width/2),source.cr+row*source.c_stride,width/2);
                }
                after=v->backend.microseconds(v->backend.context)-before;
                slot->pts=source.pts_us;slot->work_us+=after;v->queue_copy_us+=after;
            }
            v->last_decoded_pts=slot->pts;++v->count;++v->decoded;
            if(v->count>v->max_occupancy) v->max_occupancy=v->count;
        }
    }
    xf_video_service(v);
}
void xf_video_pause(xf_video *v, uint32_t now) {
    (void)now;if(v->state!=XF_VIDEO_PLAYING) return;
    clock_update(v);
    if(v->paused) {v->paused=0;if(!v->buffering)v->paused_us+=v->last_clock_us-v->pause_at;}
    else {if(!v->buffering)v->pause_at=v->last_clock_us;v->paused=1;}
    /* Pending gate waits do not include a user pause. */
    v->sync.wait_at=0;
}
unsigned xf_video_wait_us(const xf_video *v) {
    uint64_t now,timeline,remaining;
    if(v->state!=XF_VIDEO_PLAYING || v->paused) return 10000;
    now=v->backend.microseconds(v->backend.context);
    if(v->streaming && v->input_wait)return 1000;
    if(v->buffering)return 1000;
    timeline=v->clock_started?v->origin_pts+now-v->origin_us-v->paused_us:0;
    if(v->pending && v->ready && v->clock_started) {
        if(v->pending_pts<=timeline) return v->sync.mode && !v->sync.fallbacks?500:0;
        remaining=v->pending_pts-timeline;
        if(v->count<XF_VIDEO_QUEUE && !v->eof && remaining>v->decode_estimate_us+3000) return 0;
        if(v->sync.mode && !v->sync.fallbacks) return remaining<500?(unsigned)remaining:500;
        return remaining<4000?(unsigned)remaining:4000;
    }
    if(!v->pending && v->count) return 0;
    if(!v->eof && v->count<XF_VIDEO_QUEUE && (!v->clock_started || !v->pending ||
       (v->pending_pts>timeline && v->pending_pts-timeline>v->decode_estimate_us+3000))) return 0;
    return v->sync.mode && !v->sync.fallbacks?500:1000;
}
int xf_video_export(const xf_video *v, const char *path) {
    FILE *f;unsigned i,count,start;int ok;
    static const char *names[]={"late","overflow","decode","gpu","eos","present"};
    if(xf_video_active(v)) return 0;
    f=fopen(path,"wb");if(!f) return 0;
    fprintf(f,"# version=0.6,mode=%u,threshold_us=%llu,decoded=%u,presented=%u,dropped=%u,cancelled=%u,overwritten=%u\n",
        v->sync.mode,(unsigned long long)v->late_threshold_us,v->decoded,v->rendered,v->dropped,v->cancelled,v->trace_overwritten);
    fprintf(f,"# proc_avg_us=%llu,proc_p95_us=%llu,proc_max_us=%llu,drift_us=%lld,vsync_wait_us=%llu\n",
        (unsigned long long)(v->processing.count?v->processing.sum/v->processing.count:0),
        (unsigned long long)xf_time_p95(&v->processing),(unsigned long long)v->processing.max,
        (long long)v->drift_us,(unsigned long long)v->sync.wait_us);
    fprintf(f,"# drops_late=%u,overflow=%u,decode=%u,gpu=%u,eos=%u,decode_faults=%u,gpu_stalls=%u,eof_events=%u,queue_full=%u,starvation=%u,clock_faults=%u\n",
        v->drop_reason[0],v->drop_reason[1],v->drop_reason[2],v->drop_reason[3],v->drop_reason[4],v->decode_failures,v->gpu_stalls,v->eof_events,v->full_ticks,v->starved,v->clock_faults);
    fprintf(f,"# span_p95_us=%llu,span_max_us=%llu,refresh_sample_us=%u,vsync_faults=%u,half_rejected=%u\n",
        (unsigned long long)xf_time_p95(&v->frame_span),(unsigned long long)v->frame_span.max,v->sync.period_us,v->sync.faults,v->sync.half_rejected);
    fprintf(f,"event,pts_us,timeline_us,lateness_us,cpu_processing_us,prepare_span_upper_us,queue\n");
    count=v->trace_count>XF_VIDEO_TRACE?XF_VIDEO_TRACE:v->trace_count;start=v->trace_count-count;
    for(i=0;i<count;++i) {
        const xf_video_trace *t=&v->trace[(start+i)%XF_VIDEO_TRACE];
        fprintf(f,"%s,%llu,%llu,%llu,%llu,%llu,%u\n",names[t->reason],(unsigned long long)t->pts,
            (unsigned long long)t->timeline,(unsigned long long)t->lateness,(unsigned long long)t->processing,(unsigned long long)t->span,t->occupancy);
    }
    ok=!ferror(f);if(fclose(f)) ok=0;return ok;
}
