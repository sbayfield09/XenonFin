/* GPU conversion follows the GPL XMPlayer Y/U/V texture + RECTLIST path.
 * It resolves ONLY into a private tiled surface. Neither renderer changes
 * scanout or the working UI presentation; CPU copies completed tile bands. */
#include "video_native.h"
#include "video_pixels.h"
#include "framebuffer.h"
#include <xenos/xe.h>
#include <xenos/edram.h>
#include <ppc/cache.h>
#include <stdio.h>
#include <string.h>
#include "third_party/xmplayer_yuv_shaders.h"
extern uint64_t xf_monotonic_us(void);
extern uint32_t xenos_read32(int);
extern void Xe_pStep(struct XenosDevice *, int);
extern void Xe_pBlockUntilIdle(struct XenosDevice *);
static struct {
    int gpu, initialized, pending, failed, needs_clear;
    unsigned x,y,w,h,footer,token;
    uint64_t submitted, last_poll;
    xf_video_render_stats stats;
    xf_video_rect rect;
    struct XenosDevice device;
    struct XenosShader *pixel,*vertex;
    struct XenosSurface *plane[3],*target;
    struct XenosVertexBuffer *vertices;
    unsigned video_width,video_height;
} native;
static uint32_t cpu_pixels[640*384] __attribute__((aligned(128)));
static int vblank(void *context) { (void)context; return !!(xenos_read32(0x6534)&0x1000); }
static uint64_t clock_us(void *context) { (void)context; return xf_monotonic_us(); }
static int error(char *text, size_t size, const char *message) { snprintf(text,size,"%s",message); return 0; }
static int gpu_done(void) {
    volatile uint32_t *written=(volatile uint32_t *)((uintptr_t)native.device.rb+0x10100U);
    memdcbf((void *)written,128); return *written==native.token;
}
static int begin(void *context, const xf_video_info *info, char *text, size_t size) {
    (void)context;
    memset(&native.stats,0,sizeof(native.stats));
    if(!xf_framebuffer_video_area(&native.x,&native.y,&native.w,&native.h,&native.footer)) return error(text,size,"Display too small for video plus diagnostics.");
    native.rect=xf_video_fit(info,native.w,native.h);
    if(!native.gpu) return 1;
    if(native.failed) return error(text,size,"GPU test previously timed out. Use CPU mode, or reboot before retrying GPU.");
    if(native.pending && !gpu_done()) return error(text,size,"Previous GPU work is still pending. CPU mode remains available.");
    native.pending=0;
    if(!native.initialized) {
        struct XenosVBFFormat format={2,{{XE_USAGE_POSITION,0,XE_TYPE_FLOAT4},{XE_USAGE_TEXCOORD,0,XE_TYPE_FLOAT2}}};
        /* Opt-in only. Upstream Xe_Init/edram_init contain hardware waits and
         * fatal allocation paths: see NATIVE_VIDEO.md before the GPU test. */
        Xe_Init(&native.device); edram_init(&native.device);
        native.pixel=Xe_LoadShaderFromMemory(&native.device,(void *)ps);
        Xe_InstantiateShader(&native.device,native.pixel,0);
        native.vertex=Xe_LoadShaderFromMemory(&native.device,(void *)vs);
        Xe_InstantiateShader(&native.device,native.vertex,0);
        Xe_ShaderApplyVFetchPatches(&native.device,native.vertex,0,&format);
        native.vertices=Xe_CreateVertexBuffer(&native.device,4096);
        native.initialized=1;
    }
    if(!native.target || native.target->width!=(int)native.w || native.target->height!=(int)native.h) {
        if(native.target) Xe_DestroyTexture(&native.device,native.target);
        native.target=Xe_CreateTexture(&native.device,native.w,native.h,1,XE_FMT_8888|XE_FMT_BGRA,1);
        memset(native.target->base,0,native.target->wpitch*native.target->hpitch);
        memdcbst(native.target->base,native.target->wpitch*native.target->hpitch);
    }
    if(native.video_width!=info->width || native.video_height!=info->height) {
        unsigned i;
        for(i=0;i<3;++i) {
            unsigned divisor=i?2:1;
            if(native.plane[i]) Xe_DestroyTexture(&native.device,native.plane[i]);
            native.plane[i]=Xe_CreateTexture(&native.device,info->width/divisor,info->height/divisor,1,XE_FMT_8,0);
            native.plane[i]->u_addressing=native.plane[i]->v_addressing=XE_TEXADDR_CLAMP;
            memset(native.plane[i]->base,i?128:16,native.plane[i]->wpitch*native.plane[i]->hpitch);
        }
        native.video_width=info->width; native.video_height=info->height;
    }
    Xe_SetRenderTarget(&native.device,native.target); native.needs_clear=1;
    return 1;
}
static int submit(void *context, const xf_video_image *image, char *text, size_t size) {
    uint64_t start=xf_monotonic_us();
    (void)context; (void)text; (void)size;
    ++native.stats.prepared;
    if(!native.gpu) {
        xf_video_pixels(cpu_pixels,native.w,native.h,image,&native.rect);
        native.stats.conversion_us+=xf_monotonic_us()-start;
        return 1;
    } else {
        const unsigned char *planes[]={image->y,image->cb,image->cr}; unsigned i,row;
        struct vertex { float x,y,z,w,u,v; } vertices[3];
        float left=2.0f*native.rect.x/native.w-1.0f, right=2.0f*(native.rect.x+native.rect.width)/native.w-1.0f;
        float top=1.0f-2.0f*native.rect.y/native.h, bottom=1.0f-2.0f*(native.rect.y+native.rect.height)/native.h;
        vertices[0]=(struct vertex){left,top,0,1,0,0}; vertices[1]=(struct vertex){left,bottom,0,1,0,1}; vertices[2]=(struct vertex){right,top,0,1,1,0};
        for(i=0;i<3;++i) {
            unsigned w=image->width/(i?2:1), h=image->height/(i?2:1), stride=i?image->c_stride:image->y_stride;
            unsigned char *data=Xe_Surface_LockRect(&native.device,native.plane[i],0,0,0,0,XE_LOCK_WRITE);
            for(row=0;row<h;++row) memcpy(data+row*native.plane[i]->wpitch,planes[i]+row*stride,w);
            Xe_Surface_Unlock(&native.device,native.plane[i]);
        }
        native.stats.upload_us+=xf_monotonic_us()-start;
        start=xf_monotonic_us();
        memcpy(Xe_VB_Lock(&native.device,native.vertices,0,sizeof(vertices),XE_LOCK_WRITE),vertices,sizeof(vertices));
        Xe_VB_Unlock(&native.device,native.vertices);
        Xe_InvalidateState(&native.device); Xe_SetRenderTarget(&native.device,native.target);
        Xe_SetClearColor(&native.device,0xff000000);
        /* Resolve clears EDRAM for the next frame. Only clear explicitly on
         * begin; the old path emitted a second resolve/clear every frame. */
        if(native.needs_clear) { Xe_Clear(&native.device,XE_CLEAR_COLOR); native.needs_clear=0; }
        Xe_SetZEnable(&native.device,0); Xe_SetZWrite(&native.device,0); Xe_SetCullMode(&native.device,XE_CULL_NONE);
        Xe_SetAlphaTestEnable(&native.device,0); Xe_SetSrcBlend(&native.device,XE_BLEND_ONE); Xe_SetDestBlend(&native.device,XE_BLEND_ZERO);
        Xe_SetShader(&native.device,SHADER_TYPE_PIXEL,native.pixel,0); Xe_SetShader(&native.device,SHADER_TYPE_VERTEX,native.vertex,0);
        for(i=0;i<3;++i) Xe_SetTexture(&native.device,i,native.plane[i]);
        Xe_SetStreamSource(&native.device,0,native.vertices,0,sizeof(struct vertex)/4);
        Xe_DrawPrimitive(&native.device,XE_PRIMTYPE_RECTLIST,0,1);
        Xe_Resolve(&native.device); Xe_pBlockUntilIdle(&native.device);
        /* This emits a GPU ordering packet, NOT a CPU spin or VSync wait.
         * It must precede the scratch token so resolve is complete first. */
        if(!++native.token) ++native.token;
        /* Never accept a stale value from a reused writeback allocation as
         * completion of this command stream. This cache line is reserved for
         * the scratch register (the ring read pointer is a different line). */
        {
            uint32_t *written=(uint32_t *)((uintptr_t)native.device.rb+0x10100U);
            *written=~native.token; memdcbst(written,128);
#ifdef XENON
            __asm__ volatile("sync" ::: "memory");
#else
            __asm__ volatile("" ::: "memory");
#endif
        }
        Xe_pStep(&native.device,(int)native.token);
        /* Timestamp BEFORE the kick: a fast GPU may finish during Execute. */
        native.submitted=native.last_poll=xf_monotonic_us(); Xe_Execute(&native.device);
        native.pending=1;
        native.stats.commands_us+=xf_monotonic_us()-start; return 1;
    }
}
static int poll(void *context, char *text, size_t size) {
    uint64_t now,before; int done;
    (void)context;
    if(!native.gpu || !native.pending) return 1;
    before=xf_monotonic_us(); done=gpu_done(); now=xf_monotonic_us();
    if(!done) {
        /* Bracket the fence read: a negative poll's lower bound is BEFORE
         * the read; a positive poll's upper bound is AFTER the read. */
        native.last_poll=before;
        if(now-native.submitted<250000) return 0;
        native.failed=1; error(text,size,"GPU completion timeout. Private surfaces retained; CPU mode remains available."); return -1;
    }
    native.pending=0;
    /* No GPU timestamp-query API is provided by this libXenon. Completion
     * lies between last negative poll and this observation. Report the upper
     * bound AND the poll uncertainty, never mislabel it as precise GPU time. */
    native.stats.gpu_observed_us+=now-native.submitted;
    native.stats.gpu_poll_gap_us+=now-native.last_poll;
    ++native.stats.gpu_completed;
    Xe_VBReclaim(&native.device); return 1;
}
static int present(void *context, char *text, size_t size) {
    uint64_t start=xf_monotonic_us(); const void *source=cpu_pixels;
    (void)context;
    if(native.gpu) {
        if(native.pending) return error(text,size,"Attempted publication before GPU completion.");
        memdcbf(native.target->base,native.target->wpitch*native.target->hpitch);
        source=native.target->base;
    }
    if(!xf_framebuffer_video_copy(source,native.x,native.y,native.w,native.h))
        return error(text,size,"Video tile copy bounds error.");
    native.stats.copy_us+=xf_monotonic_us()-start; ++native.stats.presented; return 1;
}
static void end(void *context) {
    (void)context;
    /* An abandoned GPU render can only touch private surfaces. Keep bounded
     * allocations alive; never free memory still referenced by the GPU. */
    xf_framebuffer_invalidate();
}
void xf_video_native_backend(xf_video_backend *backend, int gpu) {
    native.gpu=gpu;
    backend->begin=begin; backend->submit=submit; backend->poll=poll; backend->end=end; backend->microseconds=clock_us; backend->context=NULL;
    backend->present=present; backend->stats=&native.stats; backend->vblank=vblank;
    backend->format=gpu?"Xenos Y/U/V textures -> tiled BGRA":"CPU BT.601 -> B,G,R,X / BE BBGGRR00";
}
