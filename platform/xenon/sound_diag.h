#ifndef XF_SOUND_DIAG_H
#define XF_SOUND_DIAG_H
#include "music.h"
enum { XF_TONE_IDLE, XF_TONE_PLAYING, XF_TONE_DRAINING, XF_TONE_DONE, XF_TONE_ERROR };
typedef struct {
    xf_audio_backend backend;
    unsigned state,frames,phase,submitted,queued,previous_queue;
    uint32_t progress_at;
    unsigned char block[2048];
    char message[160];
} xf_sound_diag;
void xf_sound_diag_init(xf_sound_diag *,const xf_audio_backend *);
int xf_sound_diag_active(const xf_sound_diag *);
int xf_sound_diag_start(xf_sound_diag *,int busy,uint32_t now);
void xf_sound_diag_tick(xf_sound_diag *,uint32_t now);
#endif
