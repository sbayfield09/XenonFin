#ifndef XF_VIDEO_PIXELS_H
#define XF_VIDEO_PIXELS_H
#include "video_decode.h"
typedef struct { unsigned x,y,width,height; } xf_video_rect;
xf_video_rect xf_video_fit(const xf_video_info *, unsigned width, unsigned height);
unsigned xf_video_tile(unsigned x, unsigned y, unsigned pitch);
void xf_video_pixels(uint32_t *tiled, unsigned width, unsigned height, const xf_video_image *, const xf_video_rect *);
#endif
