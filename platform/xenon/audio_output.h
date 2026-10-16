#ifndef XF_AUDIO_OUTPUT_H
#define XF_AUDIO_OUTPUT_H
#include <stddef.h>
#include <stdint.h>
#define XF_AUDIO_RATE 48000U
#define XF_AUDIO_FRAME_BYTES 4U
#define XF_AUDIO_BLOCK_BYTES 2048U
#define XF_AUDIO_RING_BYTES 65536U
#define XF_AUDIO_MAX_QUEUED 32768U
typedef struct {
    int (*queued)(void *), (*available)(void *);
    /* Success means the bytes were synchronously copied into owned DMA
     * storage. Failure consumes no hardware space; never count a failed write. */
    int (*submit)(void *, const unsigned char *, size_t);
    void *context;
} xf_audio_backend;
#endif
