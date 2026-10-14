#include "protocol.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

enum { CHUNK_SIZE, CHUNK_DATA, CHUNK_CR, CHUNK_LF, CHUNK_TRAILER };
static void fail(xf_response *r, const char *text) { r->failed = 1; snprintf(r->error, sizeof(r->error), "%s", text); }
static int equal(const char *a, const char *b) {
    while (*a && *b) if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    return !*a && !*b;
}
static int number(const char *s, size_t *out, unsigned base, size_t limit) {
    size_t result = 0;
    int digits = 0;
    for (; *s; ++s) {
        unsigned digit;
        if (*s >= '0' && *s <= '9') digit = (unsigned)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f') digit = (unsigned)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F') digit = (unsigned)(*s - 'A' + 10);
        else return 0;
        if (digit >= base || digit > limit || result > (limit - digit) / base) return 0;
        result = result * base + digit; digits = 1;
    }
    *out = result; return digits;
}
static void headers(xf_response *r) {
    char *line = r->header, *end = strstr(line, "\r\n");
    int encoding = 0;
    if (!end || (strncmp(line, "HTTP/1.1 ", 9) && strncmp(line, "HTTP/1.0 ", 9)) ||
        end - line < 12 || line[9] < '1' || line[9] > '5' || !isdigit((unsigned char)line[10]) || !isdigit((unsigned char)line[11]) ||
        (end - line > 12 && line[12] != ' ')) { fail(r, "Malformed HTTP status line"); return; }
    r->status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
    if (r->status < 200) { fail(r, "Informational HTTP responses are unsupported"); return; }
    line = end + 2;
    while (*line && strncmp(line, "\r\n", 2)) {
        char *colon, *value, *tail;
        end = strstr(line, "\r\n");
        if (!end) { fail(r, "Malformed HTTP headers"); return; }
        *end = 0; colon = strchr(line, ':');
        if (!colon || colon == line) { fail(r, "Malformed HTTP header field"); return; }
        *colon = 0; value = colon + 1;
        for (tail = line; *tail; ++tail)
            if (!isalnum((unsigned char)*tail) && !strchr("!#$%&'*+-.^_`|~", *tail)) { fail(r, "Invalid HTTP field name"); return; }
        while (*value == ' ' || *value == '\t') ++value;
        tail = end;
        while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t')) --tail;
        *tail = 0;
        if (equal(line, "Content-Length")) {
            if (r->has_length || !number(value, &r->remaining, 10, r->body_limit)) { fail(r, "Invalid/oversized Content-Length"); return; }
            r->has_length = 1;
        } else if (equal(line, "Transfer-Encoding")) {
            if (r->chunked || !equal(value, "chunked")) { fail(r, "Unsupported Transfer-Encoding"); return; }
            r->chunked = 1;
        } else if (equal(line, "Content-Encoding")) {
            if (encoding++ || !equal(value, "identity")) { fail(r, "Compressed HTTP response is unsupported"); return; }
        } else if (equal(line, "Content-Type")) {
            if (r->content_type[0] || strlen(value) >= sizeof(r->content_type)) { fail(r, "Invalid Content-Type"); return; }
            snprintf(r->content_type, sizeof(r->content_type), "%s", value);
        }
        line = end + 2;
    }
    if (r->chunked && r->has_length) { fail(r, "Ambiguous HTTP framing"); return; }
    r->headers_done = 1; r->phase = CHUNK_SIZE;
    if (r->status == 204 || r->status == 304 || (r->has_length && !r->remaining)) r->complete = 1;
}
void xf_response_init(xf_response *r) { memset(r, 0, sizeof(*r)); r->body_limit = XF_BODY_LIMIT; }
void xf_response_stream(xf_response *r, int (*sink)(void *, unsigned char), void *context, size_t limit) {
    xf_response_init(r); r->sink = sink; r->sink_context = context; r->body_limit = limit;
}
static void body_byte(xf_response *r, unsigned char c) {
    if (r->body_size >= r->body_limit) { fail(r, r->sink ? "Audio stream exceeds size limit" : "HTTP body exceeds 32 KiB limit"); return; }
    if (r->sink) {
        if (r->status != 200) { fail(r, "Audio stream HTTP status is not 200"); return; }
        if (!r->sink(r->sink_context, c)) { fail(r, "Audio receive ring overflow"); return; }
        ++r->body_size; return;
    }
    r->body[r->body_size++] = (char)c; r->body[r->body_size] = 0;
}
void xf_response_feed(xf_response *r, const void *bytes, size_t size) {
    const unsigned char *p = (const unsigned char *)bytes;
    size_t i;
    for (i = 0; i < size && !r->complete && !r->failed; ++i) {
        unsigned char c = p[i];
        if (!r->headers_done) {
            if (!c || (c < 32 && c != '\r' && c != '\n' && c != '\t') || c == 127) { fail(r, "Invalid HTTP header bytes"); break; }
            if (r->header_size >= XF_HEADER_LIMIT) { fail(r, "HTTP headers exceed 8 KiB limit"); break; }
            r->header[r->header_size++] = (char)c; r->header[r->header_size] = 0;
            if (r->header_size >= 4 && !memcmp(r->header + r->header_size - 4, "\r\n\r\n", 4)) headers(r);
        } else if (!r->chunked) {
            body_byte(r, c);
            if (r->has_length && !--r->remaining && !r->failed) r->complete = 1;
        } else if (r->phase == CHUNK_DATA) {
            body_byte(r, c); if (!--r->remaining) r->phase = CHUNK_CR;
        } else if (r->phase == CHUNK_CR) {
            if (c != '\r') fail(r, "Malformed chunk terminator"); else r->phase = CHUNK_LF;
        } else if (r->phase == CHUNK_LF) {
            if (c != '\n') fail(r, "Malformed chunk terminator"); else r->phase = CHUNK_SIZE;
        } else {
            if (!c || r->line_size + 1 >= sizeof(r->line)) { fail(r, "Oversized/invalid chunk line"); break; }
            r->line[r->line_size++] = (char)c;
            if (r->phase == CHUNK_TRAILER && ++r->trailer_size > XF_HEADER_LIMIT) { fail(r, "Oversized HTTP trailers"); break; }
            if (c == '\n') {
                char *extension;
                if (r->line_size < 2 || r->line[r->line_size - 2] != '\r') { fail(r, "Malformed chunk line"); break; }
                r->line[r->line_size - 2] = 0;
                if (r->phase == CHUNK_TRAILER) {
                    if (!r->line[0]) r->complete = 1;
                    else if (!strchr(r->line, ':')) fail(r, "Malformed HTTP trailer");
                } else {
                    extension = strchr(r->line, ';'); if (extension) *extension = 0;
                    if (!number(r->line, &r->remaining, 16, r->body_limit) || r->remaining > r->body_limit - r->body_size) fail(r, "Invalid/oversized chunk length");
                    else r->phase = r->remaining ? CHUNK_DATA : CHUNK_TRAILER;
                }
                r->line_size = 0;
            }
        }
    }
}
void xf_response_eof(xf_response *r) {
    if (r->complete || r->failed) return;
    if (r->headers_done && !r->chunked && !r->has_length) r->complete = 1;
    else fail(r, "Connection closed before HTTP response completed");
}
