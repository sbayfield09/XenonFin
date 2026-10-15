// Copyright 2009  Georg Lukas <georg@op-co.de>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//
// hacked to fit into libxenon by Felix Domke <tmbinc@elitedvb.net>
// This is a bit ugly, and it's not Georg's fault.

#include "video_pixels.h"
#include <string.h>
xf_video_rect xf_video_fit(const xf_video_info *info, unsigned width, unsigned height) {
    xf_video_rect r={0,0,width,height};
    double aspect=(double)info->width*info->pixel_aspect/info->height;
    if((double)width/height>aspect) r.width=(unsigned)(height*aspect+0.5);
    else r.height=(unsigned)(width/aspect+0.5);
    if(!r.width) r.width=1;
    if(!r.height) r.height=1;
    if(r.width>width) r.width=width;
    if(r.height>height) r.height=height;
    r.x=(width-r.width)/2; r.y=(height-r.height)/2; return r;
}
/* Same tile layout as the existing libXenon console, attributed in
 * framebuffer.c and docs/LIBXENON_CONSOLE_LICENSE.md. CPU staging only. */
unsigned xf_video_tile(unsigned x, unsigned y, unsigned pitch) {
    return (((y>>5)*32*pitch+((x>>5)<<10)+(x&3)+((y&1)<<2)+(((x&31)>>2)<<3)+(((y&31)>>1)<<6))^((y&8)<<2));
}
static unsigned clip(int v) { return v<0?0U:v>255?255U:(unsigned)v; }
void xf_video_pixels(uint32_t *tiled, unsigned width, unsigned height, const xf_video_image *image, const xf_video_rect *rect) {
    unsigned x,y;
    for(y=0;y<width*height;++y) tiled[y]=0xff000000U;
    for(y=0;y<rect->height;++y) {
        unsigned sy=y*image->height/rect->height;
        for(x=0;x<rect->width;++x) {
            unsigned sx=x*image->width/rect->width;
            int l=(int)image->y[sy*image->y_stride+sx]-16;
            int cb=(int)image->cb[(sy/2)*image->c_stride+sx/2]-128;
            int cr=(int)image->cr[(sy/2)*image->c_stride+sx/2]-128;
            unsigned r=clip((298*l+409*cr+128)>>8), g=clip((298*l-100*cb-208*cr+128)>>8), b=clip((298*l+516*cb+128)>>8);
            tiled[xf_video_tile(rect->x+x,rect->y+y,width)]=0xff000000U|(r<<16)|(g<<8)|b;
        }
    }
}
