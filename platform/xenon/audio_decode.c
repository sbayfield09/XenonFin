#define MINIMP3_IMPLEMENTATION
#include "audio_decode.h"
#include <stdio.h>
#include <string.h>
static void fail(xf_audio_decoder *d, const char *message) { d->failed = 1; snprintf(d->error, sizeof(d->error), "%s", message); }
void xf_audio_decoder_init(xf_audio_decoder *d) { xf_wipe(d, sizeof(*d)); mp3dec_init(&d->decoder); }
size_t xf_audio_space(void *context) { return XF_COMPRESSED_CAP - ((xf_audio_decoder *)context)->input_size; }
int xf_audio_byte(void *context, unsigned char byte) {
    xf_audio_decoder *d = context;
    if (!xf_audio_space(d)) return 0;
    d->compressed[d->input_write] = byte; d->input_write = (d->input_write + 1) % XF_COMPRESSED_CAP; ++d->input_size; return 1;
}
static void consume(xf_audio_decoder *d, size_t n) { d->input_read = (d->input_read + n) % XF_COMPRESSED_CAP; d->input_size -= n; d->consumed_bytes+=n; }
static void peek(xf_audio_decoder *d, size_t n) {
    size_t first = XF_COMPRESSED_CAP - d->input_read;
    if (first > n) first = n;
    memcpy(d->window, d->compressed + d->input_read, first); memcpy(d->window + first, d->compressed, n - first);
}
static void emit(xf_audio_decoder *d, int left, int right) {
    uint16_t l = (uint16_t)left, r = (uint16_t)right;
    /* Bytes, never native-endian int16_t stores: Xenon is big endian. */
    d->pcm[d->pcm_write] = (unsigned char)l; d->pcm[d->pcm_write + 1] = (unsigned char)(l >> 8);
    d->pcm[d->pcm_write + 2] = (unsigned char)r; d->pcm[d->pcm_write + 3] = (unsigned char)(r >> 8);
    d->pcm_write = (d->pcm_write + 4) % XF_PCM_CAP; d->pcm_size += 4; ++d->output_frames;
}
static void convert(xf_audio_decoder *d, int frames, unsigned channels) {
    int i;
    /* Requested movie format already equals the hardware rate. Avoid two
     * signed 64-bit interpolation divisions per sample on 32-bit PowerPC.
     * At phase==48000 the old interpolator is exactly the identity. */
    if(d->rate==48000) {
        for(i=0;i<frames;++i)emit(d,d->samples[i*channels],d->samples[i*channels+(channels==2)]);
        if(frames) {d->previous[0]=d->samples[(frames-1)*channels];d->previous[1]=d->samples[(frames-1)*channels+(channels==2)];d->have_previous=1;d->phase=48000;}
        d->source_frames+=(unsigned)frames;return;
    }
    for (i = 0; i < frames; ++i) {
        int left = d->samples[i * channels], right = d->samples[i * channels + (channels == 2)];
        if (!d->have_previous) { emit(d, left, right); d->phase = d->rate; d->have_previous = 1; }
        else {
            while (d->phase <= 48000) {
                int l = d->previous[0] + (int)(((int64_t)left - d->previous[0]) * d->phase / 48000);
                int r = d->previous[1] + (int)(((int64_t)right - d->previous[1]) * d->phase / 48000);
                emit(d, l, r); d->phase += d->rate;
            }
            d->phase -= 48000;
        }
        d->previous[0] = (int16_t)left; d->previous[1] = (int16_t)right; ++d->source_frames;
    }
}
void xf_audio_decode_tick(xf_audio_decoder *d, int eof) {
    unsigned budget;
    if (d->failed || d->finished) return;
    if (!d->tag_checked) {
        unsigned i, size = 0;
        if (d->input_size < 10 && !eof) return;
        if (d->input_size >= 10) {
            peek(d, 10);
            if (!memcmp(d->window, "ID3", 3)) {
                if (d->window[3] < 2 || d->window[3] > 4) { fail(d, "Unsupported ID3 version"); return; }
                for (i = 6; i < 10; ++i) {
                    if (d->window[i] & 128) { fail(d, "Invalid ID3 length"); return; }
                    size = (size << 7) | d->window[i];
                }
                if (size > 16U * 1024U * 1024U) { fail(d, "ID3 tag exceeds 16 MiB limit"); return; }
                d->tag_left = size + 10 + (d->window[3] == 4 && (d->window[5] & 16) ? 10 : 0);
            }
        }
        d->tag_checked = 1;
    }
    if (d->tag_left) {
        size_t n = d->input_size < d->tag_left ? d->input_size : d->tag_left;
        consume(d, n); d->tag_left -= (unsigned)n;
        if (d->tag_left) { if (eof) fail(d, "Truncated ID3 tag"); return; }
    }
    for (budget = 0; budget < (d->decode_budget?d->decode_budget:8) && XF_PCM_CAP - d->pcm_size >= 32768; ++budget) {
        mp3dec_frame_info_t info;
        size_t n = d->input_size < XF_DECODE_WINDOW ? d->input_size : XF_DECODE_WINDOW;
        int frames;
        if (!n) {
            if (eof) {
                uint64_t target;
                if (!d->mp3_frames) { fail(d, "Stream contains no decodable MP3 audio"); return; }
                target = (d->source_frames * 48000 + d->rate - 1) / d->rate;
                while (d->output_frames < target) emit(d, d->previous[0], d->previous[1]);
                d->finished = 1;
            }
            return;
        }
        if (!d->low_latency && n < XF_DECODE_WINDOW && !eof) return;
        if(d->low_latency && n<4 && !eof)return;
        peek(d,d->low_latency && n>=4?4:n);
        if (eof && n == 128 && !memcmp(d->window, "TAG", 3)) { consume(d, n); continue; }
        if(d->low_latency && n>=4) {
            size_t frame_size;
            if(!hdr_valid(d->window) || 4-HDR_GET_LAYER(d->window)!=3) {
                fail(d,"Movie stream is not MPEG Layer III (MP3) audio");return;
            }
            frame_size=(size_t)hdr_frame_bytes(d->window,0)+hdr_padding(d->window);
            if(frame_size<5 || frame_size>XF_DECODE_WINDOW){fail(d,"Free-format/oversized movie MP3 is unsupported");return;}
            if(n<frame_size){if(eof)fail(d,"Truncated movie MP3 frame");return;}
            /* Raw PES supplies frame-aligned MP3. minimp3 explicitly accepts
             * an exact complete frame; don't retain a 2 KiB tail until PS EOF. */
            n=frame_size;peek(d,n);
        }
        memset(&info, 0, sizeof(info));
        frames = mp3dec_decode_frame(&d->decoder, d->window, (int)n, d->samples, &info);
        if (!frames && info.frame_bytes == (int)n && !eof) {
            /* Keep a possible partial header at the window boundary. */
            info.frame_bytes -= 3;
        }
        if (info.frame_bytes <= 0 || (size_t)info.frame_bytes > n) {
            if (eof) { fail(d, "Truncated or invalid MP3 frame"); return; }
            return;
        }
        if(frames && d->on_frame)d->on_frame(d->frame_context,d->consumed_bytes+(unsigned)info.frame_offset,d->output_frames);
        consume(d, (size_t)info.frame_bytes);
        if (!frames) {
            if(d->low_latency){fail(d,"Invalid MP3 frame/reservoir; refusing an unsynchronised skip");return;}
            d->bad_bytes += (unsigned)info.frame_bytes;
            if (d->bad_bytes > 65536) { fail(d, "Invalid MP3 data (64 KiB without audio)"); return; }
            continue;
        }
        if (info.layer != 3 || info.hz < 8000 || info.hz > 48000 || (info.channels != 1 && info.channels != 2) ||
            (d->rate && d->rate != (unsigned)info.hz)) { fail(d, "Unsupported or changing MP3 audio format"); return; }
        d->rate = (unsigned)info.hz; d->channels = (unsigned)info.channels; ++d->mp3_frames; d->bad_bytes = 0;
        convert(d, frames, (unsigned)info.channels);
    }
}
size_t xf_audio_pcm_take(xf_audio_decoder *d, unsigned char *out, size_t cap) {
    size_t n = d->pcm_size < cap ? d->pcm_size : cap, first;
    first = XF_PCM_CAP - d->pcm_read; if (first > n) first = n;
    memcpy(out, d->pcm + d->pcm_read, first); memcpy(out + first, d->pcm, n - first);
    d->pcm_read = (d->pcm_read + n) % XF_PCM_CAP; d->pcm_size -= n; return n;
}
