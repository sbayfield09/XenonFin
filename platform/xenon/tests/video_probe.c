#define _POSIX_C_SOURCE 200809L
#include "../video_decode.h"
#include "../video_pixels.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static uint64_t microseconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
int main(int argc,char **argv) {
    FILE *input,*output,*rgb=NULL; unsigned char *bytes; long length; xf_video_info info; xf_video_image image;
    char error[180]; unsigned frames=0,row; uint64_t start,elapsed,pts=0; int rc;
    if(argc!=3 && argc!=4) return 2;
    if(argc==4) { rgb=fopen(argv[3],"wb");if(!rgb) return 2; }
    input=fopen(argv[1],"rb"); if(!input) return 2;
    fseek(input,0,SEEK_END);length=ftell(input);rewind(input);
    if(length<0 || length>XF_VIDEO_MAX_FILE) return 2;
    bytes=malloc((size_t)length); if(!bytes || fread(bytes,1,(size_t)length,input)!=(size_t)length) return 2; fclose(input);
    if(!xf_mpeg1_codec.open(bytes,(size_t)length,&info,error,sizeof(error))) { fprintf(stderr,"%s\n",error);free(bytes);return 1; }
    output=fopen(argv[2],"wb");if(!output) return 2;
    elapsed=0;
    for(;;) {
        start=microseconds(); rc=xf_mpeg1_codec.next(&image,error,sizeof(error)); elapsed+=microseconds()-start;
        if(rc!=1) break;
        if(frames && image.pts_us<=pts) return 3;
        pts=image.pts_us;
        for(row=0;row<image.height;++row) fwrite(image.y+row*image.y_stride,1,image.width,output);
        for(row=0;row<image.height/2;++row) fwrite(image.cb+row*image.c_stride,1,image.width/2,output);
        for(row=0;row<image.height/2;++row) fwrite(image.cr+row*image.c_stride,1,image.width/2,output);
        if(rgb) {
            static uint32_t tiles[736*480];
            xf_video_rect rect={0,0,image.width,image.height};
            unsigned pitch=(image.width+31)&~31U,height=(image.height+31)&~31U,x;
            xf_video_pixels(tiles,pitch,height,&image,&rect);
            for(row=0;row<image.height;++row) for(x=0;x<image.width;++x) {
                uint32_t p=tiles[xf_video_tile(x,row,pitch)];
                /* Read the CPU-native word as Xbox BGRA bytes, then RGB24. */
                fputc((p>>8)&255,rgb);fputc((p>>16)&255,rgb);fputc(p>>24,rgb);
            }
        }
        if(++frames>4000) return 3;
    }
    fclose(output);if(rgb)fclose(rgb);xf_mpeg1_codec.close();free(bytes);
    if(rc<0) { fprintf(stderr,"%s\n",error);return 1; }
    printf("{\"width\":%u,\"height\":%u,\"fps\":%.6f,\"par\":%.6f,\"frames\":%u,\"last_pts_us\":%llu,\"host_decode_us\":%llu}\n",info.width,info.height,info.fps,info.pixel_aspect,frames,(unsigned long long)pts,(unsigned long long)elapsed);return 0;
}
