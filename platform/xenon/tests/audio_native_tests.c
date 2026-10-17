/* Register-level regression against the pinned SDK's field masks/arithmetic.
 * This reproduces reset residue, not a fake queue which always starts at zero. */
#include "../audio_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,inits,calls,queued_bytes,wptr;static unsigned char ring[65536];
static uint64_t total;static unsigned base=0xa0000,control=0x1d08001c;
static int override_status=-1,bad_layout;static int override_free=-1;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"shared audio line %d: %s (%s)\n",__LINE__,#x,xf_audio_native_status()->error);exit(1);}}while(0)
static unsigned status(void) {
    unsigned last=(unsigned)((total/2048-1)&31),read,remaining;
    if(override_status>=0)return (unsigned)override_status;
    if(!total)return 0x8000;
    if(!queued_bytes)return 0x8000|(last<<8)|last;
    read=(unsigned)(((total-queued_bytes)/2048)&31);remaining=2048-(unsigned)((total-queued_bytes)%2048);
    return 0x8000|(remaining<<16)|(last<<8)|read;
}
unsigned xf_test_audio_register(unsigned offset){return offset==0?base:offset==8?control:status();}
uint32_t xf_test_audio_descriptor(unsigned b,unsigned i){return __builtin_bswap32(i&1?(bad_layout?0:0x80000800U):b+256+i/2*2048);}
void xenon_sound_init(void){++inits;}
/* These are the SDK formulas, including its descriptor-granular free space. */
int xenon_sound_get_unplayed(void){unsigned s=status();int n=(int)(s>>8&31)-(int)(s&31);if(n<0)n+=32;return n*2048+(int)(s>>16);}
int xenon_sound_get_free(void){unsigned s=status();int n;if(override_free>=0)return override_free;if((s&31)==(s>>8&31)&&!(s>>16))return 65536;n=(int)(s&31)*2048-(int)wptr;return n<0?n+65536:n;}
void xenon_sound_submit(void *p,int n){unsigned i;CHECK(n>0&&(unsigned)n<=65536-queued_bytes);for(i=0;i<(unsigned)n;++i)ring[(wptr+i)%65536]=((unsigned char *)p)[i];wptr=(wptr+(unsigned)n)%65536;queued_bytes+=(unsigned)n;total+=(unsigned)n;++calls;}
int main(int argc,char **argv){xf_audio_backend b,c,m;unsigned char bytes[2048];unsigned i,j;const xf_audio_native_metrics *s;const char *mode=argc>1?argv[1]:"normal";
 if(!strcmp(mode,"startup")||!strcmp(mode,"startup-timeout"))override_status=1; /* r=1,last=0 => 63488: SDK init never writes +4. */
 if(!strcmp(mode,"alignment"))base=0xa0001;
 if(!strcmp(mode,"layout"))bad_layout=1;
 xf_audio_native_init(&b);xf_audio_native_init(&c);xf_audio_native_bind(&m,XF_AUDIO_MUSIC);
 CHECK(inits==1&&b.submit==c.submit&&b.context==c.context);CHECK(xf_audio_native_status()->double_init==1);
 for(i=0;i<sizeof(bytes);++i)bytes[i]=(unsigned char)(i*13);
 if(!strcmp(mode,"alignment")||!strcmp(mode,"layout")){CHECK(xf_audio_native_status()->failed&&!calls);return 0;}
 if(!strcmp(mode,"startup")||!strcmp(mode,"startup-timeout")){
  CHECK(xenon_sound_get_unplayed()==63488);CHECK(xf_audio_native_status()->state==XF_AUDIO_STARTING&&!xf_audio_native_status()->failed);
  CHECK(b.queued(b.context)<0&&!xf_audio_acquire(&b)&&!b.submit(b.context,bytes,sizeof(bytes))&&!calls);
  xf_audio_native_service(1);
  if(!strcmp(mode,"startup-timeout")){xf_audio_native_service(2000001);s=xf_audio_native_status();CHECK(s->failed&&s->snapshots[XF_DMA_FAULT].status==1&&!calls);return 0;}
  override_status=0x8000;xf_audio_native_service(1000);CHECK(b.queued(b.context)==0&&!xf_audio_native_status()->failed);
  CHECK(xf_audio_native_status()->snapshots[XF_DMA_AFTER_INIT].status==1);override_status=-1;
 }
 CHECK(b.queued(b.context)==0&&xenon_sound_get_unplayed()==0&&xf_audio_native_status()->raw_status==0x8000);
 CHECK(xf_audio_acquire(&b));
 if(!strcmp(mode,"format")){CHECK(!b.submit(b.context,bytes,1024)&&!calls);CHECK(xf_audio_native_status()->format_errors==1);return 0;}
 if(!strcmp(mode,"overrun")){CHECK(b.submit(b.context,bytes,sizeof(bytes)));override_free=0;CHECK(!b.submit(b.context,bytes,sizeof(bytes))&&calls==1);CHECK(xf_audio_native_status()->overruns==1);return 0;}
 if(!strcmp(mode,"pause")){xf_audio_native_service(1);xf_audio_native_service(10000001);CHECK(!xf_audio_native_status()->failed);}
 CHECK(!xf_audio_acquire(&m)&&!m.submit(m.context,bytes,sizeof(bytes))&&!xf_audio_release(&m)&&!calls);
 CHECK(b.submit(b.context,bytes,sizeof(bytes)));CHECK(!xf_audio_release(&b)&&xf_audio_native_status()->owner==XF_AUDIO_TONE);
 xf_audio_native_init(&c);CHECK(inits==1&&xf_audio_native_status()->active_init==1);
 if(!strcmp(mode,"counter")||!strcmp(mode,"pointer")){
  override_status=!strcmp(mode,"counter")?0x08018000:0x08000100; /* remaining>2048 OR last-valid not submitted */
  CHECK(b.queued(b.context)<0);s=xf_audio_native_status();CHECK(s->failed&&s->snapshots[XF_DMA_FAULT].status==(unsigned)override_status);return 0;
 }
 if(!strcmp(mode,"stall")){xf_audio_native_service(1);xf_audio_native_service(2000001);CHECK(xf_audio_native_status()->failed&&inits==1);return 0;}
 queued_bytes=2047;CHECK(b.queued(b.context)==2047&&xf_audio_native_status()->consumed==0);
 queued_bytes=0;CHECK(b.queued(b.context)==0&&xf_audio_native_status()->consumed==512);
 queued_bytes=4;CHECK(b.queued(b.context)==4&&xf_audio_native_status()->consumed==512&&!xf_audio_native_status()->failed&&xf_audio_native_status()->counter_faults==1);queued_bytes=0;
 CHECK(xf_audio_release(&b)&&xf_audio_acquire(&m));
 for(i=0;i<500;++i){unsigned start=wptr;CHECK(m.submit(m.context,bytes,sizeof(bytes)));memset(bytes,0x5a,sizeof(bytes));
  CHECK(queued_bytes==2048);for(j=0;j<2048;++j)CHECK(ring[(start+j)%65536]==(i?(unsigned char)0x5a:(unsigned char)(j*13)));
  queued_bytes=0;CHECK(m.queued(m.context)==0);
 }
 CHECK(xf_audio_release(&m));s=xf_audio_native_status();CHECK(s->submitted==256512&&s->consumed==s->submitted&&s->submissions==501&&s->pointer_wraps>=15&&!s->failed&&!s->owner);
 CHECK(s->snapshots[XF_DMA_BEFORE_INIT].valid&&s->snapshots[XF_DMA_BEFORE_PCM].valid&&s->snapshots[XF_DMA_PLAYING].valid&&s->snapshots[XF_DMA_STOPPED].valid);
 CHECK(inits==1);printf("SDK status 0x8000, startup residue, leases, ownership, partial consumption and wraps: %u checks PASS\n",checks);return 0;}
