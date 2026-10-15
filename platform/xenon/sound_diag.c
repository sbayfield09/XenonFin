#include "sound_diag.h"
#include <string.h>
#include <stdio.h>
static void error(xf_sound_diag *d,const char *s){d->state=XF_TONE_ERROR;snprintf(d->message,sizeof(d->message),"%s",s);}
void xf_sound_diag_init(xf_sound_diag *d,const xf_audio_backend *b) {
    memset(d,0,sizeof(*d));d->backend=*b;
    snprintf(d->message,sizeof(d->message),"Ready: A requests 440 Hz / 200 ms; Y resets idle audio.");
}
int xf_sound_diag_active(const xf_sound_diag *d){return d->state==XF_TONE_PLAYING || d->state==XF_TONE_DRAINING;}
int xf_sound_diag_start(xf_sound_diag *d,int busy,uint32_t now) {
    int q;
    if(busy || xf_sound_diag_active(d)){snprintf(d->message,sizeof(d->message),"Audio owned by playback/tone. Stop it and wait for cleanup first.");return 0;}
    q=d->backend.queued(d->backend.context);
    if(q){error(d,"DMA is not empty (or invalid). No tone submitted; Y resets audio when playback is idle.");return 0;}
    d->state=XF_TONE_PLAYING;d->frames=d->phase=d->submitted=d->queued=d->previous_queue=0;d->progress_at=now;
    snprintf(d->message,sizeof(d->message),"Tone requested: generating real 440 Hz stereo PCM.");return 1;
}
void xf_sound_diag_tick(xf_sound_diag *d,uint32_t now) {
    unsigned budget;int q;
    if(!xf_sound_diag_active(d))return;
    q=d->backend.queued(d->backend.context);
    if(q<0 || q>65536){error(d,"Invalid hardware DMA queue reading; stop playback and use idle audio reset.");return;}
    d->queued=(unsigned)q;
    if(d->queued!=d->previous_queue){d->progress_at=now;d->previous_queue=d->queued;}
    if(d->state==XF_TONE_DRAINING && !d->queued){d->state=XF_TONE_DONE;snprintf(d->message,sizeof(d->message),"DMA reports tone drained. Confirm audible 440 Hz; counters alone do not prove sound.");return;}
    if((uint32_t)(now-d->progress_at)>=2000){error(d,"Audio DMA made no progress for 2s. UI remains active; Y resets idle audio.");return;}
    for(budget=0;budget<4 && d->frames<9600 && d->queued+sizeof(d->block)<=8192;++budget) {
        unsigned i,n=9600-d->frames;
        if(d->backend.available(d->backend.context)<4096)return;
        if(n>512)n=512;
        memset(d->block,0,sizeof(d->block));
        for(i=0;i<n;++i) {
            uint16_t sample=(uint16_t)(int16_t)((d->phase&0x80000000U)?-3000:3000);
            d->block[i*4]=d->block[i*4+2]=(unsigned char)sample;
            d->block[i*4+1]=d->block[i*4+3]=(unsigned char)(sample>>8);
            d->phase+=(uint32_t)((440ULL<<32)/48000);
        }
        d->backend.submit(d->backend.context,d->block,sizeof(d->block));
        d->frames+=n;d->submitted+=sizeof(d->block);d->queued+=sizeof(d->block);d->previous_queue=d->queued;d->progress_at=now;
    }
    if(d->frames==9600)d->state=XF_TONE_DRAINING;
}
