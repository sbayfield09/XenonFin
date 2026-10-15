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
static void fail(xf_video *v, const char *message) {
    char saved[180]; snprintf(saved,sizeof(saved),"%s",message);
    release(v); v->state=XF_VIDEO_ERROR; snprintf(v->message,sizeof(v->message),"%s",saved);
}
void xf_video_init(xf_video *v, const xf_video_codec *codec) { memset(v,0,sizeof(*v)); v->codec=codec; snprintf(v->message,sizeof(v->message),"FAT USB: xenonfin-test.m1v. Up/Down: CPU/GPU colour bars. No audio."); }
void xf_video_stop(xf_video *v) { release(v); v->state=XF_VIDEO_IDLE; v->paused=0; snprintf(v->message,sizeof(v->message),"Video stopped. B returns to Libraries."); }
int xf_video_active(const xf_video *v) { return v->state==XF_VIDEO_LOADING || v->state==XF_VIDEO_PLAYING; }
static int begin(xf_video *v) {
    if(!v->info.width || !v->info.height || v->info.width>XF_VIDEO_MAX_WIDTH || v->info.height>XF_VIDEO_MAX_HEIGHT ||
       ((v->info.width|v->info.height)&1) || !(v->info.fps>0 && v->info.fps<=30) || !(v->info.pixel_aspect>0 && v->info.pixel_aspect<10)) {
        fail(v,"Decoder returned an unsupported video format."); return 0;
    }
    if(!v->backend.present || !v->backend.begin(v->backend.context,&v->info,v->message,sizeof(v->message))) { fail(v,v->message); return 0; }
    v->backend_started=1; v->state=XF_VIDEO_PLAYING;
    v->last_clock_us=v->backend.microseconds(v->backend.context);
    snprintf(v->message,sizeof(v->message),"Silent local video. A/Start pause; X stop; B Libraries."); return 1;
}
int xf_video_start(xf_video *v, const char *path, const xf_video_backend *backend) {
    const xf_video_codec *codec=v->codec; long size;
    release(v); xf_video_init(v,codec); v->backend=*backend;
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
int xf_video_start_colours(xf_video *v, const xf_video_backend *backend) {
    const xf_video_codec *codec=v->codec;
    release(v); xf_video_init(v,codec); v->backend=*backend; v->synthetic=1;
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
    if(v->clock_started && !v->paused) v->timeline_us+=now-v->last_clock_us;
    v->last_clock_us=now; return now;
}
static int poll_pending(xf_video *v) {
    int rc;
    if(!v->pending || v->ready) return 1;
    rc=v->backend.poll(v->backend.context,v->message,sizeof(v->message));
    if(rc<0) { fail(v,v->message); return 0; }
    if(rc>0) { v->ready=1; }
    return 1;
}
static int display_ready(xf_video *v) {
    uint64_t period=(uint64_t)(1000000.0/v->info.fps);
    clock_update(v);
    if(!v->pending || !v->ready || v->paused) return 1;
    if(!v->clock_started) {
        /* Preroll: one private prepared picture plus four decoded pictures
         * (or EOF). Exclude loading, initialization and priming from clock. */
        if(!v->eof && v->count<XF_VIDEO_QUEUE) return 1;
    } else if(v->pending_pts>v->timeline_us) return 1;
    /* A late picture is discarded only if its successor is already due.
     * Queue length alone is NEVER a reason to discard a picture. */
    if(v->clock_started && v->timeline_us>v->pending_pts+2*period && v->count && v->queue[v->head].pts<=v->timeline_us) {
        ++v->dropped; v->pending=v->ready=0; return 1;
    }
    if(!v->backend.present(v->backend.context,v->message,sizeof(v->message))) { fail(v,v->message); return 0; }
    if(!v->clock_started) {
        v->clock_started=1; v->timeline_us=v->pending_pts;
        v->last_clock_us=v->backend.microseconds(v->backend.context); v->fps_at=(uint32_t)(v->last_clock_us/1000);
    }
    ++v->rendered; v->pending=v->ready=0; return 1;
}
void xf_video_tick(xf_video *v, uint32_t now) {
    uint64_t before, after; (void)now;
    if(v->state==XF_VIDEO_LOADING) {
        size_t count=v->size-v->loaded; if(count>32768) count=32768;
        if(fread(v->bytes+v->loaded,1,count,v->file)!=count) { fail(v,"USB/file read failed or clip was truncated while loading."); return; }
        v->loaded+=count;
        if(v->loaded!=v->size) return;
        fclose(v->file); v->file=NULL;
        if(!v->codec->open(v->bytes,v->size,&v->info,v->message,sizeof(v->message))) { fail(v,v->message); return; }
        begin(v); return;
    }
    if(v->state!=XF_VIDEO_PLAYING) return;
    before=clock_update(v);
    if(v->clock_started) {
        uint32_t ms=(uint32_t)(before/1000);
        if((uint32_t)(ms-v->fps_at)>=1000) {
            v->fps_milli=(unsigned)((uint64_t)(v->rendered-v->fps_frames)*1000000/(uint32_t)(ms-v->fps_at));
            v->fps_at=ms; v->fps_frames=v->rendered;
        }
    }
    if(!poll_pending(v) || !display_ready(v)) return;
    if(v->paused) return;
    /* One decode call per iteration; all other services run between calls. */
    if(!v->eof && v->count<XF_VIDEO_QUEUE) {
        xf_video_image source; int rc;
        xf_video_slot *slot=&v->queue[(v->head+v->count)%XF_VIDEO_QUEUE];
        before=v->backend.microseconds(v->backend.context);
        if(v->synthetic) { colour_frame(v,slot); rc=1; v->eof=1; }
        else rc=v->codec->next(&source,v->message,sizeof(v->message));
        after=v->backend.microseconds(v->backend.context)-before;
        if(rc<0) { fail(v,v->message); return; }
        if(!rc) v->eof=1;
        else {
            v->decode_us+=after; if(after>v->max_decode_us) v->max_decode_us=after;
            if(!v->synthetic) {
                unsigned row,width=v->info.width,height=v->info.height,n=width*height;
                if(source.width!=width || source.height!=height || source.y_stride<width || source.c_stride<width/2 ||
                   (v->decoded && source.pts_us<=v->queue[(v->head+v->count+XF_VIDEO_QUEUE-1)%XF_VIDEO_QUEUE].pts)) {
                    /* The last decoded slot remains intact even after it is
                     * consumed. Queue head/count always identify its index. */
                    fail(v,"Decoder picture dimensions/stride/timestamps changed."); return;
                }
                before=v->backend.microseconds(v->backend.context);
                for(row=0;row<height;++row) memcpy(slot->data+row*width,source.y+row*source.y_stride,width);
                for(row=0;row<height/2;++row) {
                    memcpy(slot->data+n+row*(width/2),source.cb+row*source.c_stride,width/2);
                    memcpy(slot->data+n+n/4+row*(width/2),source.cr+row*source.c_stride,width/2);
                }
                slot->pts=source.pts_us; v->queue_copy_us+=v->backend.microseconds(v->backend.context)-before;
            }
            ++v->count; ++v->decoded; if(v->count>v->max_occupancy) v->max_occupancy=v->count;
        }
    }
    clock_update(v);
    if(!v->pending && v->count) {
        xf_video_slot *slot=&v->queue[v->head];
        uint64_t period=(uint64_t)(1000000.0/v->info.fps);
        while(v->count>1 && v->clock_started && v->timeline_us>slot->pts+2*period && v->queue[(v->head+1)%XF_VIDEO_QUEUE].pts<=v->timeline_us) {
            ++v->dropped; v->head=(v->head+1)%XF_VIDEO_QUEUE; --v->count; slot=&v->queue[v->head];
        }
        /* Preparation happens AHEAD of PTS, into private memory. Publication
         * happens separately, only after readiness AND the source deadline. */
        {
            xf_video_image image=view(v,slot);
            if(!v->backend.submit(v->backend.context,&image,v->message,sizeof(v->message))) { fail(v,v->message); return; }
            v->pending=1; v->ready=0; v->pending_pts=slot->pts;
            v->head=(v->head+1)%XF_VIDEO_QUEUE; --v->count;
            if(!poll_pending(v)) return;
        }
    }
    if(!display_ready(v)) return;
    if(v->clock_started && !v->count && !v->eof && !v->pending) ++v->starved;
    if(v->eof && !v->count && !v->pending) {
        v->codec->close(); free(v->bytes); v->bytes=NULL; v->state=XF_VIDEO_ENDED;
        snprintf(v->message,sizeof(v->message),"%s",v->synthetic?"Bars: black white RED GREEN BLUE cyan magenta yellow. Up/Down CPU/GPU.":"End of clip. Final frame held. A/Y replay; B Libraries.");
    }
}
void xf_video_pause(xf_video *v, uint32_t now) {
    (void)now;
    if(v->state!=XF_VIDEO_PLAYING) return;
    clock_update(v); v->paused=!v->paused;
}
unsigned xf_video_wait_us(const xf_video *v) {
    uint64_t deadline,now,advance;
    if(v->state!=XF_VIDEO_PLAYING || v->paused) return 10000;
    if(!v->clock_started || (!v->eof && v->count<XF_VIDEO_QUEUE)) return 1000;
    if(v->pending && !v->ready) return 1000; /* cooperative GPU fence poll */
    now=v->backend.microseconds(v->backend.context); advance=now-v->last_clock_us;
    if(v->pending && v->pending_pts>v->timeline_us+advance) {
        deadline=v->pending_pts-v->timeline_us-advance;
        /* Retain a 1 ms lower bound: never spin at a sub-millisecond deadline.
         * Controller, USB, HTTP and UI are serviced between bounded waits. */
        if(deadline>4000) return 4000;
        if(deadline>1000) return (unsigned)deadline;
    }
    return 1000;
}
