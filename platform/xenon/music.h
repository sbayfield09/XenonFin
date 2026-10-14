#ifndef XF_MUSIC_H
#define XF_MUSIC_H
#include "native_net.h"
#include "browse.h"
#include "audio_decode.h"
#include "music_api.h"
typedef struct {
    int (*queued)(void *), (*available)(void *);
    void (*submit)(void *, const unsigned char *, size_t);
    void *context;
} xf_audio_backend;
enum xf_music_state { XF_MUSIC_IDLE, XF_MUSIC_NEGOTIATING, XF_MUSIC_BUFFERING, XF_MUSIC_PLAYING, XF_MUSIC_DRAINING, XF_MUSIC_FINISHED, XF_MUSIC_ERROR };
typedef struct {
    xf_http_client stream, control;
    xf_audio_decoder decoder;
    xf_audio_backend audio;
    xf_music_source source;
    xf_endpoint endpoint;
    xf_item track, next_track;
    xf_location queue, next_queue;
    xf_page lookup;
    char token[2049], user[65], device[65];
    char message[180], report_message[128];
    unsigned char output[8192];
    unsigned queue_index, queue_total, next_index, next_total, underruns;
    uint64_t submitted, valid_submitted, played;
    uint32_t report_at, drain_at;
    enum xf_music_state state;
    int paused, started, sent_start, sent_stop, cleaned, control_kind, control_pending;
    int report_due, session_expired, failure, next;
    int report_status, stream_status;
} xf_music;
void xf_music_init(xf_music *, const xf_audio_backend *);
void xf_music_forget(xf_music *);
int xf_music_play(xf_music *, const xf_endpoint *, const char *device, const char *token, const char *user,
                   const xf_item *, const xf_location *, unsigned index, unsigned total, uint32_t now);
void xf_music_tick(xf_music *, uint32_t now, int network_ready);
void xf_music_pause(xf_music *);
void xf_music_stop(xf_music *, uint32_t now);
void xf_music_skip(xf_music *, int delta, uint32_t now);
uint64_t xf_music_position(const xf_music *);
int xf_music_active(const xf_music *);
const char *xf_music_status(const xf_music *);
#endif
