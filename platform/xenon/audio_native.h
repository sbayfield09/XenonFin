#ifndef XF_AUDIO_NATIVE_H
#define XF_AUDIO_NATIVE_H
#include "audio_output.h"
typedef struct {
    uint64_t submitted,consumed; /* stereo PCM frames, lifetime since init */
    unsigned queued,free_bytes,submissions,overruns,format_errors,counter_faults;
    unsigned raw_status,pointer_wraps,last_read_pointer,seen_pointer;
    int initialized,failed;
    char error[128];
} xf_audio_native_metrics;
/* One original SDK device, initialized once in the established boot order.
 * Tone, music, movie and standalone regression use these exact callbacks. */
void xf_audio_native_init(xf_audio_backend *);
void xf_audio_native_service(uint64_t monotonic_us);
const xf_audio_native_metrics *xf_audio_native_status(void);
#endif
