#include "music.h"
#include <stdio.h>
#include <string.h>
enum { CONTROL_NONE, NEGOTIATE, START, PROGRESS, STOP, CLEANUP, LOOKUP };
static void say(xf_music *m, const char *text) { snprintf(m->message, sizeof(m->message), "%s", text); }
void xf_music_init(xf_music *m, const xf_audio_backend *backend) {
    memset(m, 0, sizeof(*m)); m->audio = *backend;
    xf_http_init(&m->stream); xf_http_init(&m->control); xf_audio_decoder_init(&m->decoder);
}
void xf_music_forget(xf_music *m) {
    xf_audio_backend audio = m->audio;
    xf_http_cancel(&m->stream); xf_http_cancel(&m->control); xf_wipe(m, sizeof(*m)); xf_music_init(m, &audio);
}
int xf_music_active(const xf_music *m) { return m->state >= XF_MUSIC_NEGOTIATING && m->state <= XF_MUSIC_DRAINING; }
uint64_t xf_music_position(const xf_music *m) { return m->played * 10000000 / 48000; }
static unsigned queued(xf_music *m) {
    int n = m->audio.queued(m->audio.context);
    return n > 0 && n <= 65536 ? (unsigned)n : 0;
}
static void clock_audio(xf_music *m) {
    uint64_t pending = queued(m) / 4, played = m->submitted > pending ? m->submitted - pending : 0;
    if (played > m->valid_submitted) played = m->valid_submitted;
    if (played > m->played) m->played = played;
}
static void control(xf_music *m, int kind, const char *method, const char *path, const char *body, uint32_t now) {
    xf_http_api(&m->control, &m->endpoint, m->device, m->token, method, path, body, now);
    m->control_kind = kind; m->control_pending = 1;
}
static void begin(xf_music *m, uint32_t now) {
    char path[160], body[1500];
    xf_audio_decoder_init(&m->decoder); xf_wipe(&m->source, sizeof(m->source));
    m->submitted = m->valid_submitted = m->played = 0;
    m->started = m->sent_start = m->sent_stop = m->cleaned = m->failure = 0;
    m->paused = 0; m->report_due = 0; m->next = 0; m->stream_status = m->report_status = 0; m->report_message[0] = 0;
    snprintf(path, sizeof(path), "/Items/%s/PlaybackInfo", m->track.id);
    if (!xf_music_profile(body, sizeof(body), m->user)) { m->state = XF_MUSIC_ERROR; say(m, "Cannot build MP3 playback profile."); return; }
    control(m, NEGOTIATE, "POST", path, body, now); m->state = XF_MUSIC_NEGOTIATING; say(m, "Negotiating authenticated MP3 playback.");
}
static void drain(xf_music *m, uint32_t now) {
    if (m->state != XF_MUSIC_DRAINING) m->drain_at = now;
    clock_audio(m); xf_http_cancel(&m->stream);
    m->decoder.input_size = m->decoder.pcm_size = 0;
    if (m->control_pending && (m->control_kind == NEGOTIATE || m->control_kind == LOOKUP || m->control_kind == PROGRESS)) {
        xf_http_cancel(&m->control); m->control_pending = 0;
    }
    m->state = XF_MUSIC_DRAINING;
}
void xf_music_stop(xf_music *m, uint32_t now) {
    m->next = 0;
    if (!xf_music_active(m)) return;
    drain(m, now); say(m, "Stopping; draining queued PCM and reporting position.");
}
static void failed(xf_music *m, const char *reason, uint32_t now) { m->failure = 1; m->next = 0; say(m, reason); drain(m, now); }
int xf_music_play(xf_music *m, const xf_endpoint *ep, const char *device, const char *token, const char *user,
                   const xf_item *track, const xf_location *location, unsigned index, unsigned total, uint32_t now) {
    if (strcmp(track->type, "Audio") || !xf_music_id(track->id) || !xf_music_id(user) || !xf_music_id(device) ||
        !token || !*token || strlen(token) >= sizeof(m->token)) return 0;
    if (xf_music_active(m)) {
        /* A new explicit selection uses the same authenticated server. */
        m->next_track = *track; m->next_queue = *location; m->next_index = index; m->next_total = total;
        m->next = 2; drain(m, now); say(m, "Switching track; finishing previous session."); return 1;
    }
    m->endpoint = *ep; snprintf(m->device, sizeof(m->device), "%s", device);
    snprintf(m->token, sizeof(m->token), "%s", token); snprintf(m->user, sizeof(m->user), "%s", user);
    m->track = *track; m->queue = *location; m->queue_index = index; m->queue_total = total;
    begin(m, now); return 1;
}
void xf_music_pause(xf_music *m) {
    if (!xf_music_active(m) || m->state == XF_MUSIC_DRAINING) return;
    m->paused = !m->paused; m->report_due = 1;
}
void xf_music_skip(xf_music *m, int delta, uint32_t now) {
    unsigned current = m->next == 1 ? m->next_index : m->queue_index;
    if (!xf_music_active(m) || (m->queue.view != XF_CHILDREN && m->queue.view != XF_TRACKS) ||
        (delta < 0 && !current) || (delta > 0 && (current >= m->queue_total || m->queue_total - current <= 1))) {
        say(m, delta < 0 ? "No previous track in this list." : "No next track in this list."); return;
    }
    m->next_index = delta < 0 ? current - 1 : current + 1; m->next = 1;
    drain(m, now); say(m, "Skipping track; finishing previous session.");
}
static void report(xf_music *m, int kind, uint32_t now) {
    char body[700]; const char *path = kind == START ? "/Sessions/Playing" : kind == STOP ? "/Sessions/Playing/Stopped" : "/Sessions/Playing/Progress";
    if (!xf_music_report(body, sizeof(body), m->track.id, &m->source, xf_music_position(m), m->paused)) {
        snprintf(m->report_message, sizeof(m->report_message), "Cannot construct playback report.");
        if (kind == START) m->sent_start = 1;
        if (kind == STOP) m->sent_stop = 1;
        return;
    }
    control(m, kind, "POST", path, body, now); m->report_at = now; m->report_due = 0;
    if (kind == START) m->sent_start = 1;
    if (kind == STOP) m->sent_stop = 1;
}
static void handle_control(xf_music *m, uint32_t now) {
    xf_response *r = &m->control.response; int kind = m->control_kind;
    m->control_pending = 0;
    if (r->status == 401) { m->session_expired = 1; failed(m, "HTTP 401: music session expired. Sign in again.", now); goto done; }
    if (m->control.error[0] || !r->complete || r->failed || r->status < 200 || r->status >= 300) {
        if (kind == NEGOTIATE || kind == LOOKUP) {
            char error[180]; snprintf(error, sizeof(error), "Music API HTTP %d: %.110s", r->status, m->control.error[0] ? m->control.error : "request denied or failed");
            failed(m, error, now);
        } else {
            m->report_status = r->status;
            snprintf(m->report_message, sizeof(m->report_message), "Playback reporting HTTP %d: %.75s", r->status, m->control.error[0] ? m->control.error : "server rejected report");
        }
        goto done;
    }
    if (kind == NEGOTIATE) {
        if (!xf_music_source_parse(r, m->track.id, &m->source)) { failed(m, "No compatible progressive MP3 source; check server permissions/profile.", now); goto done; }
        xf_http_audio(&m->stream, &m->endpoint, m->device, m->token, m->source.path, xf_audio_byte, xf_audio_space, &m->decoder, now);
        m->state = XF_MUSIC_BUFFERING; say(m, "Buffering authenticated MP3 audio.");
    } else if (kind == LOOKUP) {
        if (!xf_parse_page(r->body, r->body_size, m->next_index, 0, &m->lookup) || m->lookup.count != 1 || strcmp(m->lookup.items[0].type, "Audio")) {
            failed(m, "Next track unavailable or library changed. Reopen the album.", now); goto done;
        }
        m->track = m->lookup.items[0]; m->queue_index = m->next_index; m->queue_total = m->lookup.total;
        xf_wipe(&m->lookup, sizeof(m->lookup));
        /* begin() overwrites this response, so clear it before starting. */
        xf_wipe(r, sizeof(*r)); begin(m, now); return;
    } else { m->report_status = r->status; m->report_message[0] = 0; }
done:
    xf_wipe(r, sizeof(*r));
}
static void output_audio(xf_music *m) {
    unsigned pending = queued(m), budget;
    if (m->paused || (!m->started && pending)) return;
    if (m->state == XF_MUSIC_BUFFERING) {
        if (m->decoder.pcm_size < 16384 && !m->decoder.finished) return;
        m->state = XF_MUSIC_PLAYING;
    }
    for (budget = 0; budget < 4 && pending < 32768; ++budget) {
        size_t n = m->decoder.pcm_size < sizeof(m->output) ? m->decoder.pcm_size : sizeof(m->output), valid;
        size_t capacity = (32768 - pending) & ~2047U;
        int free_bytes = m->audio.available(m->audio.context);
        /* Only the final PCM tail may be padded. Round device capacity first,
         * otherwise an EOF drain could insert silence between data blocks. */
        if (n > capacity) n = capacity;
        if (!m->decoder.finished) n -= n % 2048;
        if (!n) return;
        if (free_bytes < 0 || (unsigned)free_bytes < ((n + 2047) & ~2047U) + 2048) return;
        valid = xf_audio_pcm_take(&m->decoder, m->output, n);
        n = (valid + 2047) & ~2047U; memset(m->output + valid, 0, n - valid);
        m->audio.submit(m->audio.context, m->output, n); m->submitted += n / 4; m->valid_submitted += valid / 4;
        pending += (unsigned)n; m->started = 1;
    }
}
void xf_music_tick(xf_music *m, uint32_t now, int ready) {
    xf_http_tick(&m->control, now, ready); xf_http_tick(&m->stream, now, ready); clock_audio(m);
    if (m->control_pending && m->control.done && !m->control.active) handle_control(m, now);
    if (m->session_expired) return;
    if (m->state == XF_MUSIC_BUFFERING || m->state == XF_MUSIC_PLAYING) {
        if (m->stream.response.headers_done) {
            const char *type = m->stream.response.content_type;
            m->stream_status = m->stream.response.status;
            if (m->stream_status == 401) { m->session_expired = 1; failed(m, "HTTP 401: music stream session expired.", now); return; }
            if (m->stream_status != 200 || (*type && strncmp(type, "audio/mpeg", 10) && strncmp(type, "audio/mp3", 9) && strncmp(type, "application/octet-stream", 24))) {
                char error[128]; snprintf(error, sizeof(error), "Unexpected music stream HTTP %d or Content-Type.", m->stream_status); failed(m, error, now);
            }
        }
        if (m->stream.done && !m->stream.active && m->stream.error[0]) failed(m, m->stream.error, now);
        if (m->state != XF_MUSIC_DRAINING && !m->paused) {
            xf_audio_decode_tick(&m->decoder, m->stream.done && !m->stream.active && !m->stream.error[0]);
            if (m->decoder.failed) failed(m, m->decoder.error, now);
            else {
                if (m->started && !queued(m) && m->decoder.pcm_size < 16384 && !m->decoder.finished && m->state == XF_MUSIC_PLAYING) {
                    ++m->underruns; m->state = XF_MUSIC_BUFFERING; say(m, "Network/decode underrun; rebuffering.");
                }
                output_audio(m);
                if (m->decoder.finished && !m->decoder.pcm_size && !queued(m)) {
                    if (m->queue_index + 1 < m->queue_total && (m->queue.view == XF_CHILDREN || m->queue.view == XF_TRACKS)) {
                        m->next_index = m->queue_index + 1; m->next = 1;
                    }
                    drain(m, now); say(m, m->next ? "Track finished; loading next track." : "Track finished.");
                }
            }
        }
    }
    if (!m->control_pending && !m->control.active) {
        if (m->started && !m->sent_start) report(m, START, now);
        else if (m->state == XF_MUSIC_DRAINING && !queued(m)) {
            if (m->started && !m->sent_stop) { report(m, STOP, now); return; }
            if (m->source.transcode && !m->cleaned) {
                char path[220]; m->cleaned = 1;
                snprintf(path, sizeof(path), "/Videos/ActiveEncodings?DeviceId=%s&PlaySessionId=%s", m->device, m->source.session_id);
                control(m, CLEANUP, "DELETE", path, NULL, now); return;
            }
            if (m->next == 2) {
                m->track = m->next_track; m->queue = m->next_queue; m->queue_index = m->next_index; m->queue_total = m->next_total;
                begin(m, now);
            } else if (m->next == 1) {
                char path[600];
                snprintf(path, sizeof(path), "/Items?UserId=%s&ParentId=%s&StartIndex=%u&Limit=1&SortBy=%s&SortOrder=Ascending&EnableImages=false&EnableUserData=true%s", m->user,
                    m->queue.view == XF_TRACKS ? m->queue.scope : m->queue.id, m->next_index,
                    m->queue.view == XF_TRACKS ? "SortName" : "IndexNumber,SortName", m->queue.view == XF_TRACKS ? "&Recursive=true&IncludeItemTypes=Audio" : "");
                control(m, LOOKUP, "GET", path, NULL, now); m->state = XF_MUSIC_NEGOTIATING;
            } else m->state = m->failure ? XF_MUSIC_ERROR : XF_MUSIC_FINISHED;
        } else if (m->started && m->state != XF_MUSIC_DRAINING && xf_music_active(m) && (m->report_due || (uint32_t)(now - m->report_at) >= 5000)) report(m, PROGRESS, now);
    }
    /* Bad hardware queue readings must not make stop/skip wait forever. */
    if (m->state == XF_MUSIC_DRAINING && queued(m) && (uint32_t)(now - m->drain_at) >= 2000) {
        m->next = 0; m->state = XF_MUSIC_ERROR; say(m, "Audio DMA did not drain within 2s. Reboot before playback.");
    }
}
const char *xf_music_status(const xf_music *m) {
    if (m->paused && xf_music_active(m) && m->state != XF_MUSIC_DRAINING) return "Paused (queued PCM may drain for up to 171 ms)";
    switch (m->state) {
    case XF_MUSIC_IDLE: return "No music selected";
    case XF_MUSIC_NEGOTIATING: return "Loading track / negotiating MP3";
    case XF_MUSIC_BUFFERING: return "Buffering";
    case XF_MUSIC_PLAYING: return "Playing decoded MP3";
    case XF_MUSIC_DRAINING: return "Stopping / switching track";
    case XF_MUSIC_FINISHED: return "Stopped / finished";
    default: return "Playback error";
    }
}
