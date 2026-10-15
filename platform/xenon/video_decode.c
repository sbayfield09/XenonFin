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
static plm_buffer_t *stream_buffer;
static unsigned stream_mode, stream_eof, stream_bad, validated_header, picture_count;
static unsigned char window[8];static size_t window_size,picture_bytes;
static xf_video_info stream_info;
static char stream_error[128];
static void close_decoder(void) { decoder=NULL; allocated=0; opened=0; output_frames=0;stream_buffer=NULL;stream_mode=stream_eof=stream_bad=validated_header=picture_count=0;window_size=picture_bytes=0;stream_error[0]=0; }
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
static int stream_fail(const char *message) {stream_bad=1;snprintf(stream_error,sizeof(stream_error),"%s",message);return 0;}
const char *xf_mpeg1_stream_error(void) {return stream_error;}
size_t xf_mpeg1_stream_used(void) {return stream_buffer?plm_buffer_get_remaining(stream_buffer):0;}
size_t xf_mpeg1_stream_space(void) {
    size_t used=xf_mpeg1_stream_used();
    return stream_buffer && !stream_bad && !stream_eof && used+8<XF_VIDEO_ES_BUFFER?XF_VIDEO_ES_BUFFER-used-8:0;
}
int xf_mpeg1_stream_byte(unsigned char byte) {
    unsigned code;
    if(!xf_mpeg1_stream_space()) return 0;
    if(window_size==sizeof(window)) memmove(window,window+1,sizeof(window)-1);else ++window_size;
    window[window_size-1]=byte;++picture_bytes;
    if(window_size==8 && !window[0] && !window[1] && window[2]==1) {
        code=window[3];
        if(code==0xb5 || code==0xba) return stream_fail("Only MPEG-1 elementary video is accepted after demux.");
        if(code==0xb3) {
            unsigned w=(window[4]<<4)|(window[5]>>4),h=((window[5]&15)<<8)|window[6],r=window[7]&15,a=window[7]>>4;
            if(!w || !h || ((w|h)&1) || w>XF_VIDEO_MAX_WIDTH || h>XF_VIDEO_MAX_HEIGHT || !r || r>5 || !a || a>14)
                return stream_fail("Transcode must be MPEG-1 <=720x480 and <=30 FPS.");
            if(validated_header && (w!=stream_info.width || h!=stream_info.height || r!=rate_num || a!=rate_den))
                return stream_fail("Changing MPEG sequence geometry/rate/aspect is unsupported.");
            stream_info.width=w;stream_info.height=h;rate_num=r;rate_den=a;validated_header=1;
        } else if(code==0) {
            unsigned type=(window[5]>>3)&7;
            if(!validated_header || !type || type>3 || (!picture_count && type!=1))return stream_fail("Invalid first MPEG picture or picture type.");
            ++picture_count;picture_bytes=8;
        }
    }
    if(picture_bytes>128U*1024U) return stream_fail("MPEG picture/header exceeds bounded 128 KiB limit.");
    /* Discard whole consumed bytes. A partial current byte stays in place;
     * subsequent bytes in this feed batch need no further compaction. */
    /* Compaction and the checked byte store cannot allocate. Keep setjmp at
     * decoder entry points, not on every byte of a 2.5 Mbit/s stream. */
    if(stream_buffer->bit_index>=8)plm_buffer_discard_read_bytes(stream_buffer);
    stream_buffer->bytes[stream_buffer->length++]=byte;stream_buffer->has_ended=0;
    return 1;
}
int xf_mpeg1_stream_end(void) {
    static const unsigned char tail[]={0,0,1,0xb7,0,0,0,0};
    if(!stream_buffer || stream_bad)return 0;
    if(stream_eof)return 1;
    if(stream_buffer->bit_index>=8)plm_buffer_discard_read_bytes(stream_buffer);
    if(stream_buffer->length+sizeof(tail)>stream_buffer->capacity)return stream_fail("No room for MPEG EOF lookahead.");
    memcpy(stream_buffer->bytes+stream_buffer->length,tail,sizeof(tail));stream_buffer->length+=sizeof(tail);
    stream_eof=1;stream_buffer->mode=PLM_BUFFER_MODE_FIXED_MEM;stream_buffer->discard_read_bytes=0;plm_buffer_signal_end(stream_buffer);return 1;
}
static int stream_header(xf_video_info *info,char *error,size_t cap) {
    static const unsigned num[]={0,24000,24,25,30000,30},den[]={1,1001,1,1,1001,1};
    if(stream_bad) {reject(error,cap,stream_error);return -1;}
    if(!stream_mode) {
        close_decoder();stream_mode=1;
        if(setjmp(failure)) {close_decoder();reject(error,cap,"Stream decoder arena exhausted.");return -1;}
        stream_buffer=plm_buffer_create_with_capacity(XF_VIDEO_ES_BUFFER);
    }
    if(!validated_header || xf_mpeg1_stream_used()<144) {
        if(stream_eof) {reject(error,cap,"Stream ended before a valid MPEG-1 sequence.");return -1;}
        return 2;
    }
    if(!decoder) {
        if(setjmp(failure)) {close_decoder();reject(error,cap,"Stream reference frame arena exhausted.");return -1;}
        decoder=plm_video_create_with_buffer(stream_buffer,1);
    }
    if(setjmp(failure)) {close_decoder();reject(error,cap,"Stream reference frame arena exhausted.");return -1;}
    if(!plm_video_has_header(decoder)) {
        if(!stream_eof)return 2; /* Optional quantisation matrices may straddle feeds. */
        reject(error,cap,"Incomplete MPEG sequence header.");return -1;
    }
    *info=stream_info;info->fps=(double)num[rate_num]/den[rate_num];info->pixel_aspect=plm_video_get_pixel_aspect_ratio(decoder);
    opened=1;return 1;
}
static int open_decoder(unsigned char *data, size_t size, xf_video_info *info, char *error, size_t cap) {
    plm_buffer_t *buffer;
    if(!data && !size)return stream_header(info,error,cap);
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
    if(stream_bad) {reject(error,cap,stream_error);return -1;}
    if(!opened) { reject(error,cap,"Video decoder is closed."); return -1; }
    if(setjmp(failure)) { close_decoder(); reject(error,cap,"Unexpected decoder allocation."); return -1; }
    frame=plm_video_decode(decoder);
    if(!frame) {
        if(stream_mode && !stream_eof)return 2;
        if(plm_video_has_ended(decoder)) return 0;
        reject(error,cap,"Incomplete or invalid MPEG-1 picture."); return -1;
    }
    image->width=frame->width; image->height=frame->height;
    image->y=frame->y.data; image->cb=frame->cb.data; image->cr=frame->cr.data;
    image->y_stride=frame->y.width; image->c_stride=frame->cb.width;
    if(stream_mode) {
        static const unsigned num[]={0,24000,24,25,30000,30},den[]={1,1001,1,1,1001,1};
        image->pts_us=xf_frame_pts(output_frames++,num[rate_num],den[rate_num]);
    } else image->pts_us=xf_frame_pts(output_frames++,rate_num,rate_den); return 1;
}
const xf_video_codec xf_mpeg1_codec={"MPEG-1 / pl_mpeg",open_decoder,next_frame,close_decoder};
