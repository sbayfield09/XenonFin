#include "video.h"
#include <stdlib.h>
#include <string.h>
static void release(xf_video *v) {
    if(v->file) fclose(v->file);
    v->file=NULL;
    v->codec->close(); free(v->bytes); v->bytes=NULL;
    if(v->backend_started) v->backend.end(v->backend.context);
    v->backend_started=0; v->count=v->pending=0;
}
static void fail(xf_video *v, const char *message) {
    char saved[180]; snprintf(saved,sizeof(saved),"%s",message);
    release(v); v->state=XF_VIDEO_ERROR; snprintf(v->message,sizeof(v->message),"%s",saved);
}
void xf_video_init(xf_video *v, const xf_video_codec *codec) { memset(v,0,sizeof(*v)); v->codec=codec; snprintf(v->message,sizeof(v->message),"Copy xenonfin-test.m1v to FAT USB root. No video audio in this test."); }
void xf_video_stop(xf_video *v) { release(v); v->state=XF_VIDEO_IDLE; v->paused=0; snprintf(v->message,sizeof(v->message),"Video stopped. B returns to Libraries."); }
int xf_video_active(const xf_video *v) { return v->state==XF_VIDEO_LOADING || v->state==XF_VIDEO_PLAYING; }
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
static xf_video_image view(const xf_video *v, const xf_video_slot *slot) {
    xf_video_image image; unsigned n=v->info.width*v->info.height;
    image.width=v->info.width; image.height=v->info.height; image.y=slot->data;
    image.cb=slot->data+n; image.cr=slot->data+n+n/4;
    image.y_stride=image.width; image.c_stride=image.width/2; image.pts_us=slot->pts; return image;
}
static int poll_pending(xf_video *v) {
    int rc=v->backend.poll(v->backend.context,v->message,sizeof(v->message));
    if(rc<0) { fail(v,v->message); return 0; }
    if(rc>0) { v->pending=0; ++v->rendered; v->render_us+=v->backend.microseconds(v->backend.context)-v->submitted_at; }
    return 1;
}
void xf_video_tick(xf_video *v, uint32_t now) {
    uint64_t before, after; unsigned elapsed;
    if(v->state==XF_VIDEO_LOADING) {
        size_t count=v->size-v->loaded; if(count>32768) count=32768;
        if(fread(v->bytes+v->loaded,1,count,v->file)!=count) { fail(v,"USB/file read failed or clip was truncated while loading."); return; }
        v->loaded+=count;
        if(v->loaded!=v->size) return;
        fclose(v->file); v->file=NULL;
        if(!v->codec->open(v->bytes,v->size,&v->info,v->message,sizeof(v->message))) { fail(v,v->message); return; }
        if(!v->info.width || !v->info.height || v->info.width>XF_VIDEO_MAX_WIDTH || v->info.height>XF_VIDEO_MAX_HEIGHT ||
           ((v->info.width|v->info.height)&1) || !(v->info.fps>0 && v->info.fps<=30) || !(v->info.pixel_aspect>0 && v->info.pixel_aspect<10)) {
            fail(v,"Decoder returned an unsupported video format."); return;
        }
        if(!v->backend.begin(v->backend.context,&v->info,v->message,sizeof(v->message))) { fail(v,v->message); return; }
        v->backend_started=1; v->state=XF_VIDEO_PLAYING; v->last_ms=v->fps_at=now;
        snprintf(v->message,sizeof(v->message),"Silent local video. A/Start pause; X stop; B Libraries."); return;
    }
    if(v->state!=XF_VIDEO_PLAYING) return;
    elapsed=(uint32_t)(now-v->last_ms); v->last_ms=now;
    if(v->clock_started && !v->paused) v->timeline_us+=(uint64_t)elapsed*1000;
    if((uint32_t)(now-v->fps_at)>=1000) {
        v->fps_milli=(unsigned)((uint64_t)(v->rendered-v->fps_frames)*1000000/(uint32_t)(now-v->fps_at));
        v->fps_at=now; v->fps_frames=v->rendered;
    }
    if(v->pending && !poll_pending(v)) return;
    if(v->paused) return;
    /* One output picture per main-loop iteration, never an unbounded catch-up
     * decode loop. Frame dimensions, picture bytes and queue memory are capped. */
    if(!v->eof && v->count<XF_VIDEO_QUEUE) {
        xf_video_image source; int rc;
        before=v->backend.microseconds(v->backend.context);
        rc=v->codec->next(&source,v->message,sizeof(v->message));
        if(rc<0) { fail(v,v->message); return; }
        if(!rc) v->eof=1;
        else {
            if(source.width!=v->info.width || source.height!=v->info.height || source.y_stride<source.width || source.c_stride<source.width/2) {
                fail(v,"Decoder picture dimensions/stride changed."); return;
            }
            xf_video_slot *slot=&v->queue[(v->head+v->count)%XF_VIDEO_QUEUE];
            unsigned row, width=v->info.width, height=v->info.height, n=width*height;
            for(row=0;row<height;++row) memcpy(slot->data+row*width,source.y+row*source.y_stride,width);
            for(row=0;row<height/2;++row) {
                memcpy(slot->data+n+row*(width/2),source.cb+row*source.c_stride,width/2);
                memcpy(slot->data+n+n/4+row*(width/2),source.cr+row*source.c_stride,width/2);
            }
            slot->pts=source.pts_us; ++v->count; ++v->decoded;
            if(v->count>v->max_occupancy) v->max_occupancy=v->count;
            after=v->backend.microseconds(v->backend.context)-before;
            v->decode_us+=after; if(after>v->max_decode_us) v->max_decode_us=after;
        }
    }
    if(!v->pending && v->count) {
        xf_video_slot *slot=&v->queue[v->head];
        uint64_t period=(uint64_t)(1000000.0/v->info.fps);
        /* Drop obsolete queued pictures; keep decoding reference pictures to
         * preserve MPEG prediction. Only due frames may reach the display. */
        while(v->count && v->timeline_us>slot->pts+2*period && !(v->eof && v->count==1)) {
            ++v->dropped; v->head=(v->head+1)%XF_VIDEO_QUEUE; --v->count; slot=&v->queue[v->head];
        }
        if(v->count && (!v->clock_started || slot->pts<=v->timeline_us)) {
            xf_video_image image=view(v,slot);
            before=v->backend.microseconds(v->backend.context);
            if(!v->backend.submit(v->backend.context,&image,v->message,sizeof(v->message))) { fail(v,v->message); return; }
            v->submitted_at=before; v->pending=1;
            if(!v->clock_started) { v->clock_started=1; v->timeline_us=slot->pts; v->last_ms=now; }
            v->head=(v->head+1)%XF_VIDEO_QUEUE; --v->count;
            /* Synchronous CPU presentation is measured now, without adding
             * the main-loop poll interval to its render time. GPU polling is
             * still nonblocking; never wait here for completion. */
            if(!poll_pending(v)) return;
        }
    }
    if(v->eof && !v->count && !v->pending) {
        v->codec->close(); free(v->bytes); v->bytes=NULL; v->state=XF_VIDEO_ENDED;
        snprintf(v->message,sizeof(v->message),"End of clip. Final frame held. A/Y replay; B Libraries.");
    }
}
void xf_video_pause(xf_video *v, uint32_t now) {
    if(v->state!=XF_VIDEO_PLAYING) return;
    if(v->clock_started && !v->paused) v->timeline_us+=(uint64_t)(uint32_t)(now-v->last_ms)*1000;
    v->last_ms=now; v->paused=!v->paused;
}
