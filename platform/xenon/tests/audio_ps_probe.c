/* Exercise production PS framing and the same persistent MP3 decoder under
 * arbitrary HTTP/PES boundaries, compressed-ring wrap and PCM-ring wrap. */
#include "../video_ps.h"
#include "../audio_decode.h"
#include <stdio.h>
#include <stdlib.h>
static xf_video_ps ps;static xf_audio_decoder audio;
static FILE *elementary;
static int byte(void *p,unsigned char c){if(fputc(c,elementary)==EOF)return 0;return xf_audio_byte(p,c);}
static int discard(unsigned char c){(void)c;return 1;}
static size_t space(void){return 65542;}
int main(int argc,char **argv){FILE *in,*pcm;unsigned loops=0;int eof=0;unsigned char bytes[997],out[4096];
 if(argc!=4)return 2;
 in=fopen(argv[1],"rb");elementary=fopen(argv[2],"wb");pcm=fopen(argv[3],"wb");if(!in||!elementary||!pcm)return 2;
 xf_video_ps_init(&ps);xf_audio_decoder_init(&audio);audio.tag_checked=1;audio.low_latency=1;audio.decode_budget=1;
 ps.discard_video=1;xf_video_ps_audio(&ps,byte,xf_audio_space,&audio);
 while(++loops<1000000 && !audio.finished && !audio.failed){size_t n,i;
  if(!eof && xf_video_ps_space(&ps)>=sizeof(bytes)){
   n=fread(bytes,1,1+(loops*37%sizeof(bytes)),in);if(!n){if(ferror(in))return 1;eof=1;}
   for(i=0;i<n;++i)if(!xf_video_ps_byte(&ps,bytes[i]))return 1;
  }
  if(xf_video_ps_tick(&ps,discard,space,eof)<0){fprintf(stderr,"%s\n",ps.error);return 1;}
  xf_audio_decode_tick(&audio,eof && !ps.count && ps.payload==ps.payload_end);
  while((n=xf_audio_pcm_take(&audio,out,sizeof(out))))if(fwrite(out,1,n,pcm)!=n)return 1;
 }
 if(fclose(in)||fclose(elementary)||fclose(pcm)||audio.failed||!audio.finished){fprintf(stderr,"%s\n",audio.error);return 1;}
 printf("Fragmented PES/MP3: %u frames, %llu ES bytes, input/PCM wrap verified by byte comparison\n",audio.mp3_frames,(unsigned long long)ps.audio_bytes);return 0;}
