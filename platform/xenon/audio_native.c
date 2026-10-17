/* Only the pinned SDK writes audio registers, descriptors or sample memory.
 * Queue masks/arithmetic below are from its xenon_sound_get_unplayed/get_free.
 * 0x8000 is outside those fields: it is NOT 32768 bytes of queued audio. */
#include "audio_native.h"
#include <stdio.h>
#include <xenon_sound/sound.h>
#ifndef XF_AUDIO_NATIVE_TEST
#include <pci/io.h>
static unsigned reg(unsigned offset){return read32(0xea001600+offset);}
#else
extern unsigned xf_test_audio_register(unsigned);
extern uint32_t xf_test_audio_descriptor(unsigned,unsigned);
#define reg xf_test_audio_register
#endif
static xf_audio_native_metrics device;
static unsigned clients[]={XF_AUDIO_NONE,XF_AUDIO_TONE,XF_AUDIO_MUSIC,XF_AUDIO_MOVIE};
static uint64_t now_us,start_at,progress_at,observed_consumed,session_start,post_fault_at;
static int watching,start_watching;
static unsigned pending(unsigned status) {
    return (((status>>8&31)-(status&31))&31)*XF_AUDIO_BLOCK_BYTES+(status>>16);
}
static void capture(xf_audio_dma_snapshot *s) {
    s->valid=1;s->at_us=now_us;s->base=reg(0);s->control=reg(8);s->status=reg(4);
    s->queued=pending(s->status);s->submitted=device.submitted;s->consumed=device.consumed;
    /* SDK readings are retained separately: each reads a later MMIO sample. */
    s->sdk_queued=device.initialized?xenon_sound_get_unplayed():-1;
    s->sdk_free=device.initialized?xenon_sound_get_free():-1;
    device.raw_status=s->status;
}
static void failure(const char *s) {
    if(!device.failed){device.snapshots[XF_DMA_FAULT]=device.current;snprintf(device.error,sizeof(device.error),"%s",s);}
    device.failed=1;device.state=XF_AUDIO_FAULT;
}
static uint32_t descriptor(unsigned base,unsigned index) {
#ifdef XF_AUDIO_NATIVE_TEST
    return xf_test_audio_descriptor(base,index);
#else
    const uint32_t *d=(const uint32_t *)(uintptr_t)(base|0x80000000U);
    return d[index];
#endif
}
static int layout(unsigned base) {
    unsigned buffer,i;
    if(!base || (base&255) || base>0x2000000-256)return 0;
    buffer=__builtin_bswap32(descriptor(base,0));
    if(!buffer || (buffer&255) || buffer>0x2000000-XF_AUDIO_RING_BYTES)return 0;
    for(i=0;i<32;++i)if(__builtin_bswap32(descriptor(base,i*2))!=buffer+i*XF_AUDIO_BLOCK_BYTES ||
        __builtin_bswap32(descriptor(base,i*2+1))!=(0x80000000U|XF_AUDIO_BLOCK_BYTES))return 0;
    device.buffer_base=buffer;return 1;
}
static int queue(void *p) {
    unsigned n,r,last,remaining;uint64_t candidate;(void)p;
    if(!device.initialized || device.failed)return -1;
    capture(&device.current);n=device.current.queued;
    r=device.current.status&31;last=device.current.status>>8&31;remaining=device.current.status>>16;
    if(device.current.base!=device.descriptor_base){++device.counter_faults;failure("DMA descriptor base changed outside the audio owner.");return -1;}
    if(!device.submissions) {
        /* SDK init sets software wptr=0 but does not write the status register.
         * A reset-time observation is not proof this app has submitted PCM.
         * Wait for the SDK's empty condition AND reset pointer zero; do not
         * fabricate an empty queue from an unknown/nonempty register value. */
        if(r || last || remaining || device.current.sdk_free!=(int)XF_AUDIO_RING_BYTES) {
            if(!device.startup_sample.valid)device.startup_sample=device.current;
            ++device.startup_observations;device.state=XF_AUDIO_STARTING;return -1;
        }
        device.state=XF_AUDIO_READY;device.error[0]=0;
    } else if(remaining>XF_AUDIO_BLOCK_BYTES || last!=((device.submitted*4/XF_AUDIO_BLOCK_BYTES-1)&31) ||
              n>XF_AUDIO_RING_BYTES || (uint64_t)n>device.submitted*4) {
        ++device.counter_faults;failure("Invalid DMA descriptor/count; see retained fault snapshot.");return -1;
    }
    device.queued=n;candidate=device.submitted-(n+3)/4;
    /* Preserve the established monotonic treatment of backward observations.
     * Descriptor corruption faults above; a stalled queue faults in service. */
    if(candidate<device.consumed)++device.counter_faults;
    else device.consumed=candidate;
    if(device.seen_pointer && r<device.last_read_pointer)++device.pointer_wraps;
    device.last_read_pointer=r;device.seen_pointer=1;
    if(n)device.snapshots[XF_DMA_PLAYING]=device.current;
    return (int)n;
}
static int available(void *p) {
    int n;(void)p;if(queue(NULL)<0)return -1;
    n=xenon_sound_get_free();
    if(n<0 || n>(int)XF_AUDIO_RING_BYTES){++device.counter_faults;failure("Invalid SDK DMA free-space counter.");return -1;}
    device.free_bytes=(unsigned)n;return n;
}
static int acquire(void *p) {
    unsigned who=p?*(unsigned *)p:XF_AUDIO_NONE;
    if(!who || device.owner){++device.ownership_conflicts;return 0;}
    if(queue(NULL)!=0 || device.state!=XF_AUDIO_READY)return 0;
    device.owner=who;session_start=device.submissions;return 1;
}
static int release(void *p) {
    unsigned who=p?*(unsigned *)p:XF_AUDIO_NONE;
    if(!who || who!=device.owner){++device.ownership_conflicts;return 0;}
    if(queue(NULL)!=0){++device.shutdown_errors;return 0;}
    device.snapshots[XF_DMA_STOPPED]=device.current;device.owner=XF_AUDIO_NONE;return 1;
}
static int submit(void *p,const unsigned char *bytes,size_t size) {
    int n,free_bytes;unsigned who=p?*(unsigned *)p:XF_AUDIO_NONE;
    if(!who || who!=device.owner){++device.ownership_conflicts;return 0;}
    if(!device.initialized || device.failed)return 0;
    if(!bytes || !size || size%XF_AUDIO_BLOCK_BYTES || size>XF_AUDIO_MAX_QUEUED) {
        ++device.format_errors;failure("PCM submit must contain complete 2048-byte stereo descriptors.");return 0;
    }
    n=queue(NULL);free_bytes=available(NULL);
    if(n<0 || free_bytes<0)return 0;
    if((size_t)n+size>XF_AUDIO_MAX_QUEUED || size+XF_AUDIO_BLOCK_BYTES>(size_t)free_bytes) {
        ++device.overruns;failure("PCM capacity violation; DMA write rejected.");return 0;
    }
    if(device.submissions==session_start)device.snapshots[XF_DMA_BEFORE_PCM]=device.current;
    xenon_sound_submit((void *)bytes,(int)size);
    device.submitted+=size/XF_AUDIO_FRAME_BYTES;++device.submissions;
    device.queued=(unsigned)n+(unsigned)size;return 1;
}
void xf_audio_native_bind(xf_audio_backend *b,unsigned owner) {
    if(owner>XF_AUDIO_MOVIE)owner=XF_AUDIO_NONE;
    *b=(xf_audio_backend){queue,available,submit,&clients[owner],acquire,release};
}
void xf_audio_native_init(xf_audio_backend *b) {
    ++device.init_calls;
    if(device.initialized){++device.double_init;if(device.owner)++device.active_init;}
    else {
        capture(&device.snapshots[XF_DMA_BEFORE_INIT]);
        xenon_sound_init();device.initialized=1;device.state=XF_AUDIO_STARTING;
        capture(&device.current);device.snapshots[XF_DMA_AFTER_INIT]=device.current;
        device.descriptor_base=device.current.base;
        if(!layout(device.descriptor_base))failure("SDK DMA descriptor/buffer alignment, range or layout invalid.");
        else queue(NULL);
    }
    xf_audio_native_bind(b,XF_AUDIO_TONE);
}
void xf_audio_native_service(uint64_t now) {
    int n;now_us=now;
    if(device.failed) {
        /* Keep the first fault/current snapshot frozen. Read MMIO at 10 Hz
         * solely to distinguish physical drain from a software fault latch.
         * No reset, queue acceptance, owner release or PCM writes occur here. */
        if(!device.post_fault_reads || now-post_fault_at>=100000) {
            unsigned saved=device.raw_status;
            capture(&device.after_fault);device.raw_status=saved;
            ++device.post_fault_reads;post_fault_at=now;
        }
        return;
    }
    n=queue(NULL);
    if(device.state==XF_AUDIO_STARTING) {
        if(!start_watching){start_at=now;start_watching=1;}
        if(now-start_at>=2000000){++device.counter_faults;failure("SDK DMA reset did not reach empty pointer zero in 2s; see startup/fault snapshots.");}
        return;
    }
    start_watching=0;if(n<0)return;
    if(!watching || !n || device.consumed!=observed_consumed) {
        watching=1;progress_at=now;observed_consumed=device.consumed;
    } else if(now-progress_at>=2000000) {
        ++device.counter_faults;failure("SDK DMA stopped consuming queued PCM for 2s; no reset attempted.");
    }
}
int xf_audio_native_pending(void){return queue(NULL);}
const xf_audio_native_metrics *xf_audio_native_status(void){return &device;}
const char *xf_audio_native_state_name(unsigned s){static const char *n[]={"COLD","STARTING","READY","FAULT"};return s<4?n[s]:"INVALID";}
const char *xf_audio_native_owner_name(unsigned s){static const char *n[]={"none","tone","music","movie"};return s<4?n[s]:"invalid";}
int xf_audio_native_export(const char *path) {
    static const char *names[]={"before-init","after-init","before-PCM","playing","stopped","fault","startup-wait","after-fault-read-only"};
    FILE *f=fopen(path,"w");unsigned i;if(!f)return 0;
    fprintf(f,"# XenonFin 0.7.6 audio state=%s owner=%s init=%u duplicate=%u active_init=%u conflicts=%u shutdown_errors=%u error=%s\n",xf_audio_native_state_name(device.state),xf_audio_native_owner_name(device.owner),device.init_calls,device.double_init,device.active_init,device.ownership_conflicts,device.shutdown_errors,device.error);
    fprintf(f,"stage,valid,at_us,base,control,status,read,last,remaining,decoded_queue,sdk_queue,sdk_free,submitted,consumed\n");
    for(i=0;i<=XF_DMA_SNAPSHOTS+1;++i){const xf_audio_dma_snapshot *s=i==XF_DMA_SNAPSHOTS+1?&device.after_fault:i==XF_DMA_SNAPSHOTS?&device.startup_sample:&device.snapshots[i];
        fprintf(f,"%s,%u,%llu,%08x,%08x,%08x,%u,%u,%u,%u,%d,%d,%llu,%llu\n",names[i],s->valid,(unsigned long long)s->at_us,s->base,s->control,s->status,s->status&31,s->status>>8&31,s->status>>16,s->queued,s->sdk_queued,s->sdk_free,(unsigned long long)s->submitted,(unsigned long long)s->consumed);}
    fprintf(f,"# post_fault_reads=%u expected_last_valid=%u fault_software_queue=%u\n",device.post_fault_reads,(unsigned)((device.submitted*4/XF_AUDIO_BLOCK_BYTES-1)&31),device.queued);
    {int ok=!ferror(f);if(fclose(f))ok=0;return ok;}
}
