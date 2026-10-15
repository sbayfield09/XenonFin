#ifndef XF_VIDEO_DECODE_H
#define XF_VIDEO_DECODE_H
#include <stddef.h>
#include <stdint.h>
#define XF_VIDEO_MAX_WIDTH 720U
#define XF_VIDEO_MAX_HEIGHT 480U
#define XF_VIDEO_MAX_FILE (8U*1024U*1024U)
#define XF_VIDEO_FRAME_BYTES (XF_VIDEO_MAX_WIDTH*XF_VIDEO_MAX_HEIGHT*3U/2U)
typedef struct { unsigned width, height; double fps, pixel_aspect; } xf_video_info;
typedef struct {
    const unsigned char *y, *cb, *cr;
    unsigned width, height, y_stride, c_stride;
    uint64_t pts_us;
} xf_video_image;
/* Codec vtable keeps the player independent of MPEG-1/pl_mpeg. One instance. */
typedef struct {
    const char *name;
    int (*open)(unsigned char *, size_t, xf_video_info *, char *, size_t);
    int (*next)(xf_video_image *, char *, size_t); /* 1 frame, 0 EOF, -1 error */
    void (*close)(void);
} xf_video_codec;
extern const xf_video_codec xf_mpeg1_codec;
#endif
