#include "music_api.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
int xf_music_id(const char *s) {
    size_t i, n = s ? strlen(s) : 0;
    if (!n || n > 64) return 0;
    for (i = 0; i < n; ++i) if (!isalnum((unsigned char)s[i]) && s[i] != '-') return 0;
    return 1;
}
static int fit(int n, size_t cap) { return n >= 0 && (size_t)n < cap; }
int xf_music_profile(char *out, size_t cap, const char *user) {
    if (!xf_music_id(user)) return 0;
    return fit(snprintf(out, cap,
        "{\"UserId\":\"%s\",\"StartTimeTicks\":0,\"IsPlayback\":true,\"AutoOpenLiveStream\":false,"
        "\"EnableDirectPlay\":true,\"EnableDirectStream\":false,\"EnableTranscoding\":true,"
        "\"AllowAudioStreamCopy\":false,\"MaxStreamingBitrate\":320000,\"DeviceProfile\":{"
        "\"Name\":\"XenonFin XeLL MP3\",\"MaxStreamingBitrate\":320000,\"MusicStreamingTranscodingBitrate\":192000,"
        "\"DirectPlayProfiles\":[{\"Type\":\"Audio\",\"Container\":\"mp3\",\"AudioCodec\":\"mp3\"}],"
        "\"TranscodingProfiles\":[{\"Type\":\"Audio\",\"Container\":\"mp3\",\"Protocol\":\"http\",\"AudioCodec\":\"mp3\","
        "\"Context\":\"Streaming\",\"MaxAudioChannels\":\"2\"}],\"SubtitleProfiles\":[]}}", user), cap);
}
static int field(xf_json_ref obj, const char *key, char *out, size_t cap) {
    return xf_json_string(obj.data, obj.size, key, out, cap) == 1;
}
static int flag(xf_json_ref obj, const char *key) {
    xf_json_ref r; int yes = 0;
    return xf_json_get(obj, key, &r) == 1 && xf_json_bool(r, &yes) && yes;
}
static int small_number(xf_json_ref obj, const char *key, uint64_t *n) {
    xf_json_ref r; return xf_json_get(obj, key, &r) == 1 && xf_json_uint(r, n);
}
static int direct_mp3(xf_json_ref source) {
    char container[32], protocol[32]; xf_json_ref array, streams[16]; size_t count, i;
    int audio = 0;
    if (!flag(source, "SupportsDirectPlay") || !field(source, "Container", container, sizeof(container)) || strcmp(container, "mp3") ||
        !field(source, "Protocol", protocol, sizeof(protocol)) || strcmp(protocol, "File") ||
        xf_json_get(source, "MediaStreams", &array) != 1 || !xf_json_elements(array, 0, streams, 16, &count) || count > 16) return 0;
    for (i = 0; i < count; ++i) {
        char type[32], codec[32]; uint64_t rate, channels;
        if (!field(streams[i], "Type", type, sizeof(type))) return 0;
        if (!strcmp(type, "Audio")) {
            if (audio || !field(streams[i], "Codec", codec, sizeof(codec)) || strcmp(codec, "mp3") ||
                !small_number(streams[i], "SampleRate", &rate) || rate < 8000 || rate > 48000 ||
                !small_number(streams[i], "Channels", &channels) || channels < 1 || channels > 2) return 0;
            audio = 1;
        } else if (!strcmp(type, "Video") && !flag(streams[i], "IsAttachedPic")) return 0;
    }
    return audio;
}
static int equal_key(const char *s, size_t n, const char *key) {
    size_t i; if (strlen(key) != n) return 0;
    for (i = 0; i < n; ++i) if (tolower((unsigned char)s[i]) != tolower((unsigned char)key[i])) return 0;
    return 1;
}
static int progressive_path(const char *url, char *out, size_t cap) {
    const char *query, *p; size_t used, n = strlen(url);
    if (n < 8 || n > 1500 || url[0] != '/' || url[1] == '/' ||
        !equal_key(url, 7, "/audio/") || strstr(url, ".m3u") || strstr(url, ".mpd")) return 0;
    for (p = url; *p; ++p) if ((unsigned char)*p < 33 || (unsigned char)*p > 126 || *p == '\\' || *p == '#') return 0;
    query = strchr(url, '?'); used = query ? (size_t)(query - url) : n;
    if (used + 1 >= cap) return 0;
    memcpy(out, url, used); out[used] = 0;
    if (!query) return 1;
    p = query + 1;
    while (*p) {
        const char *end = strchr(p, '&'), *eq;
        size_t length, key_size;
        if (!end) end = p + strlen(p);
        length = (size_t)(end - p); eq = memchr(p, '=', length); key_size = eq ? (size_t)(eq - p) : length;
        /* Jellyfin may include an API key in its URL. Use only our header. */
        if (length && !equal_key(p, key_size, "api_key") && !equal_key(p, key_size, "apikey") && !equal_key(p, key_size, "accesstoken")) {
            if (used + length + 2 > cap) return 0;
            out[used++] = strchr(out, '?') ? '&' : '?'; memcpy(out + used, p, length); used += length; out[used] = 0;
        }
        p = *end ? end + 1 : end;
    }
    return 1;
}
int xf_music_source_parse(const xf_response *r, const char *id, xf_music_source *out) {
    xf_json_ref root = { r->body, r->body_size }, array, sources[8], error;
    size_t count, i; char session[65];
    xf_wipe(out, sizeof(*out));
    if (!xf_music_id(id) || !r->complete || r->failed || r->status != 200 ||
        !field(root, "PlaySessionId", session, sizeof(session)) || !xf_music_id(session) ||
        xf_json_get(root, "MediaSources", &array) != 1 || !xf_json_elements(array, 0, sources, 8, &count) || !count || count > 8) return 0;
    if (xf_json_get(root, "ErrorCode", &error) == 1 && !(error.size == 4 && !memcmp(error.data, "null", 4))) return 0;
    for (i = 0; i < count; ++i) {
        char source[65], url[1501], protocol[32]; int n;
        if (!field(sources[i], "Id", source, sizeof(source)) || !xf_music_id(source)) continue;
        if (direct_mp3(sources[i])) {
            n = snprintf(out->path, sizeof(out->path), "/Audio/%s/stream.mp3?Static=true&MediaSourceId=%s&PlaySessionId=%s", id, source, session);
            if (!fit(n, sizeof(out->path))) return 0;
            out->transcode = 0;
        } else {
            if (!flag(sources[i], "SupportsTranscoding") || !field(sources[i], "TranscodingUrl", url, sizeof(url)) ||
                !field(sources[i], "TranscodingSubProtocol", protocol, sizeof(protocol)) || strcmp(protocol, "http") ||
                !progressive_path(url, out->path, sizeof(out->path))) continue;
            out->transcode = 1;
        }
        strcpy(out->source_id, source); strcpy(out->session_id, session); return 1;
    }
    return 0;
}
int xf_music_report(char *out, size_t cap, const char *id, const xf_music_source *s, uint64_t ticks, int paused) {
    if (!xf_music_id(id) || !xf_music_id(s->source_id) || !xf_music_id(s->session_id)) return 0;
    return fit(snprintf(out, cap, "{\"ItemId\":\"%s\",\"MediaSourceId\":\"%s\",\"PlaySessionId\":\"%s\","
        "\"PositionTicks\":%llu,\"IsPaused\":%s,\"CanSeek\":false,\"PlayMethod\":\"%s\","
        "\"VolumeLevel\":100,\"IsMuted\":false,\"RepeatMode\":\"RepeatNone\"}",
        id, s->source_id, s->session_id, (unsigned long long)ticks, paused ? "true" : "false", s->transcode ? "Transcode" : "DirectPlay"), cap);
}
