/* Independent sine/format/DMA-consumption tests. Hardware output remains a
 * physical acceptance gate; this model never declares audible correctness. */
#include "../sound_diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static xf_sound_diag d;
static unsigned checks,pending,submitted_frames,consumed_frames;
static unsigned char ring[65536];static int bad_queue,free_override=-1;
static const unsigned frequencies[XF_SOUND_CASES][2]={{440,440},{880,880},{1000,1000},{0,0},{440,0},{0,440},{440,880}};
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"sound diag line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int queued(void *p){(void)p;return bad_queue?bad_queue:(int)pending;}
static int available(void *p){(void)p;return free_override>=0?free_override:65536-(int)pending;}
static int reference(unsigned hz,unsigned frame) {
    return hz?(int)floor(6000*sin(6.283185307179586476925286766559*hz*(frame%48000)/48000)+.5):0;
}
static int sample(const unsigned char *b){return (int16_t)((unsigned)b[0]|((unsigned)b[1]<<8));}
static void submit(void *p,const unsigned char *bytes,size_t size) {
    unsigned i,ch;(void)p;
    CHECK(d.verified && size==2048 && pending+size<=8192 && size<=65536-pending);
    for(i=0;i<512;++i)for(ch=0;ch<2;++ch) {
        unsigned ordinal=submitted_frames+i;
        int expected=ordinal<d.target_frames?reference(frequencies[d.mode][ch],ordinal):0;
        CHECK(sample(bytes+i*4+ch*2)==expected);
        /* Never overwrite a sample still owned by the modeled hardware. */
        ring[((submitted_frames+i)*4+ch*2)%sizeof(ring)]=bytes[i*4+ch*2];
        ring[((submitted_frames+i)*4+ch*2+1)%sizeof(ring)]=bytes[i*4+ch*2+1];
    }
    submitted_frames+=512;pending+=(unsigned)size;
}
static void consume(unsigned frames) {
    unsigned i,ch;if(frames>pending/4)frames=pending/4;
    for(i=0;i<frames;++i)for(ch=0;ch<2;++ch) {
        unsigned ordinal=consumed_frames+i,offset=(ordinal*4+ch*2)%sizeof(ring);
        int expected=ordinal<d.target_frames?reference(frequencies[d.mode][ch],ordinal):0;
        CHECK(sample(ring+offset)==expected);
    }
    pending-=frames*4;consumed_frames+=frames;
}
static void reset(const xf_audio_backend *b) {
    pending=submitted_frames=consumed_frames=0;bad_queue=0;free_override=-1;xf_sound_diag_init(&d,b);
}
static void run(const xf_audio_backend *b,unsigned mode,unsigned seconds,unsigned rate) {
    uint64_t now=1,frac=0;unsigned ticks=0;
    reset(b);CHECK(xf_sound_diag_select(&d,mode,seconds));CHECK(xf_sound_diag_start(&d,0,0));
    while(xf_sound_diag_active(&d) && ++ticks<125000) {
        xf_sound_diag_tick_us(&d,now);CHECK(pending<=8192);
        frac+=rate;consume((unsigned)(frac/1000));frac%=1000;now+=1000;
    }
    CHECK(d.state==XF_TONE_DONE && d.verified && d.frames==seconds*48000 && !d.underruns);
    CHECK(d.played==submitted_frames && consumed_frames==submitted_frames && !pending);
    CHECK(submitted_frames==((seconds*48000+511)/512)*512);
    CHECK(strstr(d.message,"UNCONFIRMED"));
    if(seconds==60) {
        CHECK(d.rate_frames>=48000 && d.rate_us);
        CHECK(fabs((double)d.rate_frames*1000000/d.rate_us-rate)<2);
        CHECK(d.max_service_gap_us==1000);
    }
}
int main(int argc,char **argv) {
    xf_audio_backend b={queued,available,submit,NULL};unsigned mode,i,ch;
    reset(&b);CHECK(!xf_sound_diag_start(&d,1,0)&&!submitted_frames);
    pending=4096;CHECK(!xf_sound_diag_start(&d,0,0)&&!submitted_frames);pending=0;
    CHECK(!xf_sound_diag_select(&d,7,1)&&!xf_sound_diag_select(&d,0,2));
    for(mode=0;mode<XF_SOUND_CASES;++mode) {
        reset(&b);CHECK(xf_sound_diag_select(&d,mode,1)&&xf_sound_diag_prepare(&d));
        for(i=0;i<48000;++i)for(ch=0;ch<2;++ch)CHECK(sample(d.pcm+i*4+ch*2)==reference(frequencies[mode][ch],i));
        if(argc>1) {
            char path[512];FILE *f;snprintf(path,sizeof(path),"%s/case-%u.s16le",argv[1],mode);
            f=fopen(path,"wb");CHECK(f && fwrite(d.pcm,1,sizeof(d.pcm),f)==sizeof(d.pcm));CHECK(!fclose(f));
        }
        run(&b,mode,1,48000);
    }
    run(&b,0,60,48000);run(&b,1,60,24000); /* Report slow hardware; never retune sine frequency. */
    reset(&b);CHECK(xf_sound_diag_start(&d,0,0));xf_sound_diag_tick_us(&d,1);CHECK(pending==8192);
    CHECK(!xf_sound_diag_select(&d,1,60));xf_sound_diag_stop(&d);CHECK(d.cancelled);
    consume(pending/4);xf_sound_diag_tick_us(&d,50001);CHECK(d.state==XF_TONE_DONE && d.frames==2048);
    reset(&b);CHECK(xf_sound_diag_start(&d,0,0));xf_sound_diag_tick_us(&d,1);
    xf_sound_diag_tick_us(&d,2000001);CHECK(d.state==XF_TONE_ERROR && strstr(d.message,"no progress"));
    reset(&b);CHECK(xf_sound_diag_start(&d,0,0));bad_queue=65537;xf_sound_diag_tick_us(&d,1);CHECK(d.state==XF_TONE_ERROR&&!submitted_frames);
    reset(&b);CHECK(xf_sound_diag_start(&d,0,0));free_override=2048;xf_sound_diag_tick_us(&d,1);CHECK(!submitted_frames);
    reset(&b);CHECK(xf_sound_diag_start(&d,0,0));xf_sound_diag_tick_us(&d,1);free_override=0;
    consume(100);xf_sound_diag_tick_us(&d,2001);CHECK(d.played==100);
    pending+=200;xf_sound_diag_tick_us(&d,3001);CHECK(d.played==100 && d.counter_faults==1 && !d.rate_us);
    printf("Sine S16LE/stereo preflight, all samples, silence/channels, descriptor ownership/wrap, 60s consumption and slow-clock diagnostics: %u checks PASS (audible Xbox output UNTESTED)\n",checks);
    return 0;
}
