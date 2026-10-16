/* Real shared adapter, mocked original SDK boundary; no guessed MMIO control. */
#include "../audio_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,inits,calls,pending,wptr,rptr;static unsigned char ring[65536];
static int inject_queue=-1,inject_free=-1;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"shared audio line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void xenon_sound_init(void){++inits;}
int xenon_sound_get_unplayed(void){return inject_queue>=0?inject_queue:(int)pending;}
int xenon_sound_get_free(void){return inject_free>=0?inject_free:65536-(int)pending;}
void xenon_sound_submit(void *p,int n){unsigned i;CHECK(n>0 && (unsigned)n<=65536-pending);for(i=0;i<(unsigned)n;++i)ring[(wptr+i)%65536]=((unsigned char *)p)[i];wptr=(wptr+(unsigned)n)%65536;pending+=(unsigned)n;++calls;}
uint32_t xf_test_audio_status(void){return (rptr/2048)&31;}
int main(int argc,char **argv){xf_audio_backend b,c;unsigned char bytes[2048];unsigned i,j;const xf_audio_native_metrics *s;
 xf_audio_native_init(&b);xf_audio_native_init(&c);CHECK(inits==1&&b.submit==c.submit&&b.queued==c.queued);
 for(i=0;i<sizeof(bytes);++i)bytes[i]=(unsigned char)(i*13);
 if(argc>1){
  if(!strcmp(argv[1],"format")){CHECK(!b.submit(NULL,bytes,1024)&&!calls);s=xf_audio_native_status();CHECK(s->failed&&s->format_errors==1&&!s->submitted);}
  else if(!strcmp(argv[1],"counter")){inject_queue=65537;CHECK(b.queued(NULL)<0);s=xf_audio_native_status();CHECK(s->failed&&s->counter_faults);}
  else if(!strcmp(argv[1],"pause")){xf_audio_native_service(1);xf_audio_native_service(10000001);CHECK(!xf_audio_native_status()->failed);CHECK(b.submit(NULL,bytes,sizeof(bytes)));xf_audio_native_service(10000002);pending=0;xf_audio_native_service(10001002);s=xf_audio_native_status();CHECK(!s->failed&&s->consumed==512&&inits==1);}
  else if(!strcmp(argv[1],"partial")){CHECK(b.submit(NULL,bytes,sizeof(bytes)));pending=2047;CHECK(b.queued(NULL)==2047&&xf_audio_native_status()->consumed==0);pending=0;CHECK(xf_audio_native_status()->consumed==512);}
  else if(!strcmp(argv[1],"stall")){CHECK(b.submit(NULL,bytes,sizeof(bytes)));xf_audio_native_service(1);xf_audio_native_service(2000001);s=xf_audio_native_status();CHECK(s->failed&&s->counter_faults&&calls==1&&inits==1);}
  else {inject_free=0;CHECK(!b.submit(NULL,bytes,sizeof(bytes))&&!calls);s=xf_audio_native_status();CHECK(s->failed&&s->overruns&&!s->submitted);}
  puts("Shared output rejects invalid format/counter/overrun without DMA writes PASS");return 0;
 }
 for(i=0;i<500;++i){CHECK(b.submit(NULL,bytes,sizeof(bytes)));memset(bytes,0x5a,sizeof(bytes));
  CHECK(pending==2048);for(j=0;j<2048;++j)CHECK(ring[(rptr+j)%65536]==(i?(unsigned char)0x5a:(unsigned char)(j*13)));
  pending=0;rptr=(rptr+2048)%65536;CHECK(c.queued(NULL)==0);
 }
 s=xf_audio_native_status();CHECK(s->submitted==256000&&s->consumed==s->submitted&&s->submissions==500&&s->pointer_wraps>=15&&!s->failed);
 CHECK(inits==1);printf("Shared SDK adapter once-only init, synchronous copy/ownership, full ring wraps and consumption: %u checks PASS\n",checks);return 0;}
