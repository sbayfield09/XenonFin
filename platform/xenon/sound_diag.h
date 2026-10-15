#ifndef XF_SOUND_DIAG_H
#define XF_SOUND_DIAG_H
#include "music.h"
#define XF_SOUND_RATE 48000U
#define XF_SOUND_FRAME_BYTES 4U
#define XF_SOUND_BLOCK_BYTES 2048U
#define XF_SOUND_QUEUE_BYTES 8192U
#define XF_SOUND_CASES 7U
enum { XF_TONE_IDLE, XF_TONE_PLAYING, XF_TONE_DRAINING, XF_TONE_DONE, XF_TONE_ERROR };
typedef struct {
    xf_audio_backend backend;
    unsigned state,mode,seconds,frames,target_frames,submitted,queued,played;
    unsigned underruns,counter_faults,verified,pcm_hash,cancelled;
    uint64_t progress_at,started_at,finished_at,last_tick,max_service_gap_us;
    uint64_t rate_at,rate_us;unsigned rate_begin,rate_frames;
    unsigned char pcm[XF_SOUND_RATE*XF_SOUND_FRAME_BYTES] __attribute__((aligned(256)));
    unsigned char block[XF_SOUND_BLOCK_BYTES] __attribute__((aligned(256)));
    char message[160];
} xf_sound_diag;
void xf_sound_diag_init(xf_sound_diag *,const xf_audio_backend *);
const char *xf_sound_diag_name(unsigned mode);
int xf_sound_diag_select(xf_sound_diag *,unsigned mode,unsigned seconds);
int xf_sound_diag_active(const xf_sound_diag *);
int xf_sound_diag_start(xf_sound_diag *,int busy,uint32_t now);
void xf_sound_diag_stop(xf_sound_diag *);
void xf_sound_diag_tick_us(xf_sound_diag *,uint64_t now_us);
void xf_sound_diag_tick(xf_sound_diag *,uint32_t now);
/* Exposed for independent PCM verification; frequencies are fixed test cases. */
int xf_sound_diag_prepare(xf_sound_diag *);
#endif
