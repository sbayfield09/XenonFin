#ifndef XF_AUDIO_DECODE_H
#define XF_AUDIO_DECODE_H
#include "protocol.h"
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#include "third_party/minimp3/minimp3.h"
#define XF_COMPRESSED_CAP (128U * 1024U)
#define XF_PCM_CAP (128U * 1024U)
#define XF_DECODE_WINDOW 16384U
typedef struct {
    mp3dec_t decoder;
    unsigned char compressed[XF_COMPRESSED_CAP], pcm[XF_PCM_CAP], window[XF_DECODE_WINDOW];
    int16_t samples[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t input_read, input_write, input_size, pcm_read, pcm_write, pcm_size;
    unsigned tag_left, phase, rate, channels, mp3_frames, bad_bytes;
    int tag_checked, have_previous, finished, failed;
    int16_t previous[2];
    uint64_t source_frames, output_frames;
    char error[128];
} xf_audio_decoder;
void xf_audio_decoder_init(xf_audio_decoder *);
size_t xf_audio_space(void *);
int xf_audio_byte(void *, unsigned char);
/* At most eight MP3 frames per tick. No network or audio driver calls. */
void xf_audio_decode_tick(xf_audio_decoder *, int eof);
size_t xf_audio_pcm_take(xf_audio_decoder *, unsigned char *, size_t);
#endif
