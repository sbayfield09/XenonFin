#include "video_decode.h"
#include "timing.h"
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
/* The low-level fixed-memory API only needs a buffer object, decoder object
 * and three reference pictures. A resettable arena bounds all decoder memory.
 * Allocation failure escapes before upstream's unchecked memset, never NULL. */
static unsigned char arena[2U*1024U*1024U] __attribute__((aligned(16)));
static size_t allocated;
static jmp_buf failure;
static void *video_alloc(size_t size) {
    void *p;
    size=(size+15U)&~(size_t)15U;
    if (size>sizeof(arena)-allocated) longjmp(failure,1);
    p=arena+allocated; allocated+=size; return p;
}
static void *video_realloc(void *p, size_t size) { (void)p; (void)size; longjmp(failure,1); return NULL; }
#define PLM_NO_STDIO
#define PLM_MALLOC(n) video_alloc(n)
#define PLM_REALLOC(p,n) video_realloc(p,n)
#define PLM_FREE(p) ((void)(p))
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg/pl_mpeg.h"
static plm_video_t *decoder;
static int opened;
static unsigned output_frames, rate_num, rate_den;
static void close_decoder(void) { decoder=NULL; allocated=0; opened=0; output_frames=0; }
static int reject(char *error, size_t cap, const char *message) { snprintf(error,cap,"%s",message); return 0; }
/* Restrict this bring-up decoder to locally generated, small elementary video.
 * Validate EVERY repeated sequence header before allowing library allocation.
 * MPEG-2 extensions, program streams, resolution/rate changes are explicit errors. */
static int preflight(const unsigned char *data, size_t size, xf_video_info *info, char *error, size_t cap) {
    size_t i, last_picture=0; unsigned rate=0, aspect=0, pictures=0;
    memset(info,0,sizeof(*info));
    if(size<144 || size>XF_VIDEO_MAX_FILE || memcmp(data,"\x00\x00\x01\xb3",4))
        return reject(error,cap,"Use a 144-byte to 8 MiB MPEG-1 elementary .m1v file.");
    for(i=0;i+7<size;++i) if(!data[i] && !data[i+1] && data[i+2]==1) {
        unsigned code=data[i+3];
        if(code==0xb5 || code==0xba) return reject(error,cap,"MPEG-2/program streams unsupported; use the documented MPEG-1 command.");
        if(code==0xb3) {
            unsigned w=(data[i+4]<<4)|(data[i+5]>>4), h=((data[i+5]&15)<<8)|data[i+6];
            unsigned r=data[i+7]&15, a=data[i+7]>>4;
            if(!w || !h || (w&1) || (h&1) || w>XF_VIDEO_MAX_WIDTH || h>XF_VIDEO_MAX_HEIGHT || !r || r>5 || !a || a>14)
                return reject(error,cap,"Need even dimensions <=720x480, MPEG-1 23.976 to 30 FPS, valid pixel aspect.");
            if(info->width && (w!=info->width || h!=info->height || r!=rate || a!=aspect))
                return reject(error,cap,"Changing video format within a test clip is unsupported.");
            info->width=w; info->height=h; rate=r; aspect=a;
        }
        if(code==0x00) {
            unsigned type=(data[i+5]>>3)&7;
            if(!type || type>3 || (!pictures && type!=1)) return reject(error,cap,"Invalid picture type or missing initial intra picture.");
            if(pictures && i-last_picture>128U*1024U) return reject(error,cap,"Compressed picture exceeds the 128 KiB bring-up limit.");
            ++pictures; last_picture=i;
        }
    }
    if(!pictures || size-last_picture>128U*1024U) return reject(error,cap,"Missing or oversized compressed picture.");
    return 1;
}
static int open_decoder(unsigned char *data, size_t size, xf_video_info *info, char *error, size_t cap) {
    plm_buffer_t *buffer;
    close_decoder();
    if(!preflight(data,size,info,error,cap)) return 0;
    if(setjmp(failure)) { close_decoder(); return reject(error,cap,"Video decoder arena exhausted (2 MiB limit)."); }
    buffer=plm_buffer_create_with_memory(data,size,0);
    decoder=plm_video_create_with_buffer(buffer,1);
    if(!plm_video_has_header(decoder)) { close_decoder(); return reject(error,cap,"Truncated MPEG-1 sequence header."); }
    {
        static const unsigned num[]={0,24000,24,25,30000,30}, den[]={1,1001,1,1,1001,1};
        unsigned rate=data[7]&15;
        rate_num=num[rate]; rate_den=den[rate];
        info->fps=(double)rate_num/rate_den;
    }
    info->pixel_aspect=plm_video_get_pixel_aspect_ratio(decoder);
    opened=1; return 1;
}
static int next_frame(xf_video_image *image, char *error, size_t cap) {
    plm_frame_t *frame;
    if(!opened) { reject(error,cap,"Video decoder is closed."); return -1; }
    if(setjmp(failure)) { close_decoder(); reject(error,cap,"Unexpected decoder allocation."); return -1; }
    frame=plm_video_decode(decoder);
    if(!frame) {
        if(plm_video_has_ended(decoder)) return 0;
        reject(error,cap,"Incomplete or invalid MPEG-1 picture."); return -1;
    }
    image->width=frame->width; image->height=frame->height;
    image->y=frame->y.data; image->cb=frame->cb.data; image->cr=frame->cr.data;
    image->y_stride=frame->y.width; image->c_stride=frame->cb.width;
    image->pts_us=xf_frame_pts(output_frames++,rate_num,rate_den); return 1;
}
const xf_video_codec xf_mpeg1_codec={"MPEG-1 / pl_mpeg",open_decoder,next_frame,close_decoder};
