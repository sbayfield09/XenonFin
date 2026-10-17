#ifndef XF_AUDIO_NATIVE_H
#define XF_AUDIO_NATIVE_H
#include "audio_output.h"
enum { XF_AUDIO_COLD, XF_AUDIO_STARTING, XF_AUDIO_READY, XF_AUDIO_FAULT };
enum { XF_AUDIO_NONE, XF_AUDIO_TONE, XF_AUDIO_MUSIC, XF_AUDIO_MOVIE };
enum { XF_DMA_BEFORE_INIT, XF_DMA_AFTER_INIT, XF_DMA_BEFORE_PCM, XF_DMA_PLAYING, XF_DMA_STOPPED, XF_DMA_FAULT, XF_DMA_SNAPSHOTS };
typedef struct {
    unsigned valid,base,control,status,queued;
    int sdk_queued,sdk_free;
    uint64_t at_us,submitted,consumed;
} xf_audio_dma_snapshot;
typedef struct {
    uint64_t submitted,consumed;
    unsigned queued,free_bytes,submissions,overruns,format_errors,counter_faults;
    unsigned raw_status,pointer_wraps,last_read_pointer,seen_pointer;
    unsigned state,owner,init_calls,double_init,active_init,ownership_conflicts,shutdown_errors;
    unsigned descriptor_base,buffer_base,startup_observations;
    int initialized,failed;
    xf_audio_dma_snapshot snapshots[XF_DMA_SNAPSHOTS],current,startup_sample;
    char error[160];
} xf_audio_native_metrics;
void xf_audio_native_init(xf_audio_backend *); /* boots once; binds tone client */
void xf_audio_native_bind(xf_audio_backend *,unsigned owner);
void xf_audio_native_service(uint64_t monotonic_us);
int xf_audio_native_pending(void); /* global read only; <0 means not ready/fault */
const xf_audio_native_metrics *xf_audio_native_status(void);
const char *xf_audio_native_state_name(unsigned);
const char *xf_audio_native_owner_name(unsigned);
int xf_audio_native_export(const char *path);
#endif
