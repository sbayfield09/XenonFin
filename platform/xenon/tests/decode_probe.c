/* Decode actual compressed MP3 with the identical scalar decoder/resampler
 * compiled into the ELF, writing its exact little-endian stereo PCM bytes. */
#include "../audio_decode.h"
#include <stdio.h>
#include <stdlib.h>
static xf_audio_decoder decoder;
int main(int argc,char **argv) {
    FILE *input,*output; unsigned char bytes[4096],pcm[8192]; size_t n,i; unsigned loops=0; int eof=0;
    if(argc!=3)return 1;
    input=fopen(argv[1],"rb");output=fopen(argv[2],"wb");if(!input||!output)return 1;
    xf_audio_decoder_init(&decoder);
    while(!decoder.finished&&!decoder.failed&&++loops<1000000){
        if(!eof&&xf_audio_space(&decoder)>=sizeof(bytes)){
            n=fread(bytes,1,sizeof(bytes),input);for(i=0;i<n;++i)if(!xf_audio_byte(&decoder,bytes[i]))return 1;
            if(n<sizeof(bytes)){if(ferror(input))return 1;eof=1;}
        }
        xf_audio_decode_tick(&decoder,eof);
        while((n=xf_audio_pcm_take(&decoder,pcm,sizeof(pcm))))if(fwrite(pcm,1,n,output)!=n)return 1;
    }
    fclose(input);fclose(output);
    if(!decoder.finished||decoder.failed){fprintf(stderr,"Native MP3 decode failed: %s\n",decoder.error);return 1;}
    printf("{\"input_rate\":%u,\"channels\":%u,\"mp3_frames\":%u,\"source_frames\":%llu,\"output_frames\":%llu}\n",
        decoder.rate,decoder.channels,decoder.mp3_frames,(unsigned long long)decoder.source_frames,(unsigned long long)decoder.output_frames);
    return 0;
}
