/* Exercise the REAL native CPU/GPU adapter. Mock only device calls, clock and
 * final display tile copy; GPU writes must stay in a private resolve target. */
#include "../video_native.h"
#include "../video_pixels.h"
#include <xenos/xe.h>
#include <xenos/edram.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"native video line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned char scratch[0x20000] __attribute__((aligned(128)));
static unsigned char vertex_bytes[4096] __attribute__((aligned(128)));
static struct XenosVertexBuffer vb;
static struct XenosShader shaders[2];
static struct XenosSurface *target,*bound[3];
static unsigned shader_count,inits,blits,invalidations,draws,reclaims,steps;
static uint64_t now;
static uint32_t token;
static int automatically_complete=1;
uint64_t xf_monotonic_us(void) { return now; }
int xf_framebuffer_video_area(unsigned *x,unsigned *y,unsigned *w,unsigned *h,unsigned *footer) {*x=32;*y=128;*w=640;*h=384;*footer=33;return 1;}
int xf_framebuffer_video_copy(const void *pixels,unsigned x,unsigned y,unsigned w,unsigned h) {
    const uint32_t *p=pixels;CHECK(x==32 && y==128 && w==640 && h==384);++blits;
    if(!draws) { CHECK(p[xf_video_tile(0,0,640)]==0xff000000); CHECK(p[xf_video_tile(320,192,640)]==0xffffffff); }
    else CHECK(p[0]==0xffabcdef);
    return 1;
}
void xf_framebuffer_invalidate(void) { ++invalidations; }
void memdcbst(void *p,int size) { CHECK(p!=NULL && size>0); }
void memdcbf(void *p,int size) { CHECK(p!=NULL && size>0); }
void Xe_Init(struct XenosDevice *d) {memset(d,0,sizeof(*d));d->rb=scratch;++inits;}
void edram_init(struct XenosDevice *d) {(void)d;}
struct XenosShader *Xe_LoadShaderFromMemory(struct XenosDevice *d,void *p) {(void)d;CHECK((*(uint32_t *)p>>16)==0x102a && shader_count<2);return &shaders[shader_count++];}
void Xe_InstantiateShader(struct XenosDevice *d,struct XenosShader *s,unsigned index) {(void)d;CHECK(s!=NULL && index==0);}
void Xe_ShaderApplyVFetchPatches(struct XenosDevice *d,struct XenosShader *s,unsigned index,const struct XenosVBFFormat *f) {(void)d;(void)s;CHECK(index==0 && f->num==2 && f->e[0].fmt==XE_TYPE_FLOAT4 && f->e[1].fmt==XE_TYPE_FLOAT2);}
struct XenosVertexBuffer *Xe_CreateVertexBuffer(struct XenosDevice *d,int length) {(void)d;CHECK(length==4096);vb.base=vertex_bytes;return &vb;}
struct XenosSurface *Xe_CreateTexture(struct XenosDevice *d,unsigned w,unsigned h,unsigned levels,int format,int tiled) {
    struct XenosSurface *s=calloc(1,sizeof(*s));(void)d;CHECK(s!=NULL && levels==1);
    s->width=(int)w;s->height=(int)h;s->format=format;s->tiled=tiled;s->bypp=(format&XE_FMT_MASK)==XE_FMT_8?1:4;
    s->wpitch=(w*s->bypp+127)&~127U;s->hpitch=(h+31)&~31U;s->base=calloc(s->hpitch,s->wpitch);CHECK(s->base!=NULL);return s;
}
void Xe_DestroyTexture(struct XenosDevice *d,struct XenosSurface *s) {(void)d;free(s->base);free(s);}
void Xe_SetRenderTarget(struct XenosDevice *d,struct XenosSurface *s) {(void)d;CHECK(s!=NULL && s->tiled && s->bypp==4);target=s;}
void *Xe_Surface_LockRect(struct XenosDevice *d,struct XenosSurface *s,int x,int y,int w,int h,int flags) {(void)d;CHECK(s!=NULL && !x && !y && !w && !h && flags==XE_LOCK_WRITE);return s->base;}
void Xe_Surface_Unlock(struct XenosDevice *d,struct XenosSurface *s) {(void)d;(void)s;}
void *Xe_VB_Lock(struct XenosDevice *d,struct XenosVertexBuffer *b,int offset,int size,int flags) {(void)d;CHECK(b==&vb && !offset && size==3*24 && flags==XE_LOCK_WRITE);return vertex_bytes;}
void Xe_VB_Unlock(struct XenosDevice *d,struct XenosVertexBuffer *b) {(void)d;(void)b;}
void Xe_InvalidateState(struct XenosDevice *d) {(void)d;}
void Xe_SetClearColor(struct XenosDevice *d,uint32_t c) {(void)d;CHECK(c==0xff000000);}
void Xe_Clear(struct XenosDevice *d,int flags) {(void)d;CHECK(flags==XE_CLEAR_COLOR);}
#define NOOP_INT(name) void name(struct XenosDevice *d,int v) {(void)d;(void)v;}
NOOP_INT(Xe_SetZEnable)
NOOP_INT(Xe_SetZWrite)
NOOP_INT(Xe_SetAlphaTestEnable)
void Xe_SetCullMode(struct XenosDevice *d,unsigned int v) {(void)d;CHECK(v==XE_CULL_NONE);}
void Xe_SetSrcBlend(struct XenosDevice *d,unsigned int v) {(void)d;CHECK(v==XE_BLEND_ONE);}
void Xe_SetDestBlend(struct XenosDevice *d,unsigned int v) {(void)d;CHECK(v==XE_BLEND_ZERO);}
void Xe_SetShader(struct XenosDevice *d,int type,struct XenosShader *s,int instance) {(void)d;CHECK((type==SHADER_TYPE_PIXEL || type==SHADER_TYPE_VERTEX) && s!=NULL && instance==0);}
void Xe_SetTexture(struct XenosDevice *d,int index,struct XenosSurface *s) {(void)d;CHECK(index>=0 && index<3 && s->format==XE_FMT_8 && !s->tiled);bound[index]=s;}
void Xe_SetStreamSource(struct XenosDevice *d,int index,struct XenosVertexBuffer *b,int offset,int stride) {(void)d;CHECK(index==0 && b==&vb && !offset && stride==6);}
void Xe_DrawPrimitive(struct XenosDevice *d,int type,int start,int count) {
    float *v=(float *)vertex_bytes;(void)d;CHECK(type==XE_PRIMTYPE_RECTLIST && start==0 && count==1);
    CHECK(v[0]==-0.8f && v[1]==1.0f && v[6]==-0.8f && v[7]==-1.0f && (v[12]>0.7999f && v[12]<0.8001f) && v[13]==1.0f);++draws;
    CHECK(*(unsigned char *)bound[0]->base==235 && *(unsigned char *)bound[1]->base==77 && *(unsigned char *)bound[2]->base==199);
}
void Xe_Resolve(struct XenosDevice *d) {(void)d;CHECK(target->width==640 && target->height==384);}
void Xe_pBlockUntilIdle(struct XenosDevice *d) {(void)d;}
void Xe_pStep(struct XenosDevice *d,int value) {(void)d;token=(uint32_t)value;++steps;}
void Xe_Execute(struct XenosDevice *d) {
    unsigned i;(void)d;CHECK(target && target->tiled);
    for(i=0;i<(unsigned)(target->wpitch*target->hpitch/4);++i)((uint32_t *)target->base)[i]=0xffabcdef;
    if(automatically_complete) *(uint32_t *)(scratch+0x10100)=token;
}
void Xe_VBReclaim(struct XenosDevice *d) {(void)d;++reclaims;}
/* Intentionally NO Xe_Sync or Xe_SetFrameBufferSurface implementation. */
int main(void) {
    static unsigned char y[320*240],cb[160*120],cr[160*120];
    xf_video_image image={y,cb,cr,320,240,320,160,0};xf_video_info info={320,240,30,1};xf_video_backend backend;
    char message[180];unsigned before;
    memset(y,235,sizeof(y));memset(cb,128,sizeof(cb));memset(cr,128,sizeof(cr));
    xf_video_native_backend(&backend,0);CHECK(backend.begin(NULL,&info,message,sizeof(message)));
    CHECK(backend.submit(NULL,&image,message,sizeof(message)) && backend.poll(NULL,message,sizeof(message))==1);
    CHECK(blits==1 && !inits);backend.end(NULL);CHECK(invalidations==1);
    memset(cb,77,sizeof(cb));memset(cr,199,sizeof(cr));
    xf_video_native_backend(&backend,1);CHECK(backend.begin(NULL,&info,message,sizeof(message)));
    CHECK(inits==1 && blits==1);CHECK(backend.submit(NULL,&image,message,sizeof(message)));CHECK(blits==1);
    CHECK(backend.poll(NULL,message,sizeof(message))==1 && blits==2 && reclaims==1 && steps==1);
    automatically_complete=0;CHECK(backend.submit(NULL,&image,message,sizeof(message)));before=blits;
    now=100000;CHECK(backend.poll(NULL,message,sizeof(message))==0 && blits==before);
    *(uint32_t *)(scratch+0x10100)=token;CHECK(backend.poll(NULL,message,sizeof(message))==1 && blits==before+1);
    CHECK(backend.submit(NULL,&image,message,sizeof(message)));now+=250000;
    CHECK(backend.poll(NULL,message,sizeof(message))==-1 && strstr(message,"timeout"));backend.end(NULL);
    xf_video_native_backend(&backend,0);CHECK(backend.begin(NULL,&info,message,sizeof(message))); /* Error does not gate CPU/UI. */
    /* Use GPU's pixel check only for simulated GPU output. */
    draws=0;memset(cb,128,sizeof(cb));memset(cr,128,sizeof(cr));CHECK(backend.submit(NULL,&image,message,sizeof(message)));backend.end(NULL);
    xf_video_native_backend(&backend,1);CHECK(!backend.begin(NULL,&info,message,sizeof(message)) && strstr(message,"previously timed out"));
    printf("Native CPU/GPU uploads, private resolve, completion, timeout and CPU recovery: %u assertions passed\n",checks);return 0;
}
