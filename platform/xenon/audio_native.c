/* Shared integration of the hardware-tested 0.7.3 standalone output path.
 * The pinned SDK remains the ONLY hardware/DMA/cache implementation. */
#include "audio_native.h"
#include <stdio.h>
#include <xenon_sound/sound.h>
#ifndef XF_AUDIO_NATIVE_TEST
#include <pci/io.h>
#else
extern uint32_t xf_test_audio_status(void);
#endif
static xf_audio_native_metrics device;
static uint64_t progress_at,observed_consumed;
static int watching;
static void failure(const char *s){device.failed=1;snprintf(device.error,sizeof(device.error),"%s",s);}
static int queue(void *p) {
    int pending;uint64_t candidate;(void)p;
    if(!device.initialized || device.failed)return -1;
    pending=xenon_sound_get_unplayed();
    if(pending<0 || pending>(int)XF_AUDIO_RING_BYTES || (uint64_t)(unsigned)pending>device.submitted*4) {
        ++device.counter_faults;failure("Invalid SDK DMA queue counter; stop playback and reboot.");return -1;
    }
    device.queued=(unsigned)pending;candidate=device.submitted-(device.queued+3)/4;
    if(candidate<device.consumed)++device.counter_faults;
    else device.consumed=candidate;
#ifndef XF_AUDIO_NATIVE_TEST
    device.raw_status=read32(0xea001604);
#else
    device.raw_status=xf_test_audio_status();
#endif
    if(device.seen_pointer && (device.raw_status&31)<device.last_read_pointer)++device.pointer_wraps;
    device.last_read_pointer=device.raw_status&31;device.seen_pointer=1;
    return pending;
}
static int available(void *p) {
    int n;(void)p;if(!device.initialized || device.failed)return -1;
    n=xenon_sound_get_free();
    if(n<0 || n>(int)XF_AUDIO_RING_BYTES){++device.counter_faults;failure("Invalid SDK DMA free-space counter.");return -1;}
    device.free_bytes=(unsigned)n;return n;
}
static int submit(void *p,const unsigned char *bytes,size_t size) {
    int pending,free_bytes;(void)p;
    if(!device.initialized || device.failed)return 0;
    if(!bytes || !size || size%XF_AUDIO_BLOCK_BYTES || size>XF_AUDIO_MAX_QUEUED) {
        ++device.format_errors;failure("PCM submit must contain complete 2048-byte stereo descriptors.");return 0;
    }
    pending=queue(NULL);free_bytes=available(NULL);
    if(pending<0 || free_bytes<0)return 0;
    if((size_t)pending+size>XF_AUDIO_MAX_QUEUED || size+XF_AUDIO_BLOCK_BYTES>(size_t)free_bytes) {
        ++device.overruns;failure("PCM output ownership/capacity violation; DMA write rejected.");return 0;
    }
    /* SDK memcpy + cache stores finish before return; caller can reuse scratch.
     * No new register writes, descriptor edits, byte swaps or resampling here. */
    xenon_sound_submit((void *)bytes,(int)size);
    device.submitted+=size/XF_AUDIO_FRAME_BYTES;++device.submissions;
    device.queued=(unsigned)pending+(unsigned)size;return 1;
}
void xf_audio_native_init(xf_audio_backend *backend) {
    if(!device.initialized){xenon_sound_init();device.initialized=1;}
    *backend=(xf_audio_backend){queue,available,submit,NULL};
}
void xf_audio_native_service(uint64_t now) {
    int pending=queue(NULL);if(pending<0)return;
    if(!watching || !pending || device.consumed!=observed_consumed) {
        watching=1;progress_at=now;observed_consumed=device.consumed;
    } else if(now-progress_at>=2000000) {
        ++device.counter_faults;failure("SDK DMA stopped consuming queued PCM for 2s; reboot before audio.");
    }
}
const xf_audio_native_metrics *xf_audio_native_status(void){if(device.initialized && !device.failed)queue(NULL);return &device;}
