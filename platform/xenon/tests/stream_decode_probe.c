/* Real PS framing and the production incremental MPEG decoder, no file-sized
 * input allocation. Deliberately fragmented input exercises packet/header splits. */
#include "../video_ps.h"
#include "../video_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static xf_video_ps ps;
int main(int argc,char **argv) {
    FILE *in,*out;xf_video_info info;xf_video_image image;char error[180];unsigned frames=0,row,loops=0;int eof=0,ended=0,header=0,rc;
    unsigned char buffer[4093];
    if(argc!=3)return 2;
    in=fopen(argv[1],"rb");out=fopen(argv[2],"wb");if(!in || !out)return 2;
    xf_video_ps_init(&ps);rc=xf_mpeg1_codec.open(NULL,0,&info,error,sizeof(error));if(rc!=2)return 3;
    while(++loops<1000000) {
        if(!eof && xf_video_ps_space(&ps)>=sizeof(buffer)) {
            size_t n=fread(buffer,1,(loops%sizeof(buffer))+1,in),i;
            if(!n) {if(ferror(in))return 2;eof=1;}
            for(i=0;i<n;++i)if(!xf_video_ps_byte(&ps,buffer[i]))return 3;
        }
        if(!ended) {
            if(xf_video_ps_tick(&ps,xf_mpeg1_stream_byte,xf_mpeg1_stream_space,eof)<0) {fprintf(stderr,"PS: %s; ES: %s\n",ps.error,xf_mpeg1_stream_error());return 1;}
            if(eof && !ps.count && ps.payload==ps.payload_end) {if(!xf_mpeg1_stream_end())return 1;ended=1;}
        }
        if(!header) {
            rc=xf_mpeg1_codec.open(NULL,0,&info,error,sizeof(error));if(rc<0){fprintf(stderr,"header: %s\n",error);return 1;}
            if(rc==1)header=1;else continue;
        }
        rc=xf_mpeg1_codec.next(&image,error,sizeof(error));
        if(rc<0){fprintf(stderr,"decode: %s\n",error);return 1;}
        if(!rc)break;
        if(rc==2)continue;
        for(row=0;row<image.height;++row)fwrite(image.y+row*image.y_stride,1,image.width,out);
        for(row=0;row<image.height/2;++row)fwrite(image.cb+row*image.c_stride,1,image.width/2,out);
        for(row=0;row<image.height/2;++row)fwrite(image.cr+row*image.c_stride,1,image.width/2,out);
        ++frames;
    }
    fclose(in);fclose(out);xf_mpeg1_codec.close();
    if(loops==1000000 || !header || !frames)return 1;
    printf("{\"frames\":%u,\"fps\":%.6f,\"width\":%u,\"height\":%u,\"ps_peak\":%u,\"received\":%llu}\n",frames,info.fps,info.width,info.height,(unsigned)ps.peak,(unsigned long long)ps.received);return 0;
}
