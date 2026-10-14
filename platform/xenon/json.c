#include "protocol.h"
#include <string.h>

typedef struct {
    const unsigned char *at, *end;
    char *out;
    size_t capacity;
    int found;
} parser;
static void space(parser *p) { while (p->at < p->end && (*p->at == ' ' || *p->at == '\r' || *p->at == '\n' || *p->at == '\t')) ++p->at; }
static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int quad(parser *p, unsigned *value) {
    int i, n; *value = 0;
    for (i = 0; i < 4; ++i) {
        if (p->at == p->end || (n = hex(*p->at++)) < 0) return 0;
        *value = (*value << 4) | (unsigned)n;
    }
    return 1;
}
static int append(char *out, size_t cap, size_t *used, unsigned char c) {
    if (out) { if (*used + 1 >= cap) return 0; out[*used] = (char)c; }
    ++*used; return 1;
}
static int string(parser *p, char *out, size_t cap) {
    size_t used = 0;
    if (p->at == p->end || *p->at++ != '"') return 0;
    while (p->at < p->end) {
        unsigned c = *p->at++;
        if (c == '"') { if (out) out[used] = 0; return 1; }
        if (c < 32) return 0;
        if (c == '\\') {
            const char *escape, *value;
            if (p->at == p->end) return 0;
            c = *p->at++;
            escape = "\"\\/bfnrt"; value = "\"\\/\b\f\n\r\t";
            if (c == 'u') {
                unsigned low;
                if (!quad(p, &c) || !c) return 0; /* NUL cannot be safely represented in our C strings. */
                if (c >= 0xd800 && c <= 0xdbff) {
                    if (p->end - p->at < 6 || p->at[0] != '\\' || p->at[1] != 'u') return 0;
                    p->at += 2;
                    if (!quad(p, &low) || low < 0xdc00 || low > 0xdfff) return 0;
                    c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
                } else if (c >= 0xdc00 && c <= 0xdfff) return 0;
                if (c < 128) { if (!append(out, cap, &used, (unsigned char)c)) return 0; }
                else {
                    if (c > 0xffff && !append(out, cap, &used, (unsigned char)(0xf0 | (c >> 18)))) return 0;
                    if (c > 0x7ff && !append(out, cap, &used, (unsigned char)((c > 0xffff ? 0x80 : 0xe0) | ((c >> 12) & (c > 0xffff ? 63 : 15))))) return 0;
                    if (!append(out, cap, &used, (unsigned char)((c > 0x7ff ? 0x80 : 0xc0) | ((c >> 6) & (c > 0x7ff ? 63 : 31)))) ||
                        !append(out, cap, &used, (unsigned char)(0x80 | (c & 63)))) return 0;
                }
                continue;
            }
            { const char *at = strchr(escape, (int)c); if (!at) return 0; c = (unsigned char)value[at - escape]; }
        } else if (c >= 128) {
            unsigned count = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
            unsigned original = c, code = c & (count == 3 ? 7 : count == 2 ? 15 : 31), i;
            if (c < 0xc2 || c > 0xf4 || p->end - p->at < (int)count || !append(out, cap, &used, (unsigned char)c)) return 0;
            for (i = 0; i < count; ++i) {
                unsigned next = *p->at++;
                if ((next & 0xc0) != 0x80 || !append(out, cap, &used, (unsigned char)next)) return 0;
                code = (code << 6) | (next & 63);
            }
            if ((original >= 0xe0 && code < 0x800) || (original >= 0xf0 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
            continue;
        }
        if (!append(out, cap, &used, (unsigned char)c)) return 0;
    }
    return 0;
}
static int value(parser *p, const char *path, unsigned depth) {
    unsigned char c;
    if (depth > 24) return 0;
    space(p); if (p->at == p->end) return 0; c = *p->at;
    if (path && !*path) {
        if (p->found || !string(p, p->out, p->capacity)) return 0;
        p->found = 1; return 1;
    }
    if (c == '{' || c == '[') {
        unsigned char end = c == '{' ? '}' : ']';
        ++p->at; space(p);
        if (p->at < p->end && *p->at == end) { ++p->at; return 1; }
        for (;;) {
            const char *child = NULL;
            if (c == '{') {
                char key[128]; size_t len;
                if (!string(p, key, sizeof(key))) return 0;
                space(p); if (p->at == p->end || *p->at++ != ':') return 0;
                len = path ? strcspn(path, ".") : 0;
                if (path && strlen(key) == len && !strncmp(key, path, len)) child = path + len + (path[len] == '.');
            }
            if (!value(p, child, depth + 1)) return 0;
            space(p); if (p->at == p->end) return 0;
            if (*p->at == end) { ++p->at; return 1; }
            if (*p->at++ != ',') return 0;
            space(p);
        }
    }
    if (c == '"') return string(p, NULL, 0);
    if (c == 't' || c == 'f' || c == 'n') {
        const char *literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
        size_t n = strlen(literal);
        if ((size_t)(p->end - p->at) < n || memcmp(p->at, literal, n)) return 0;
        p->at += n; return 1;
    }
    if (c == '-') { ++p->at; if (p->at == p->end) return 0; }
    if (*p->at == '0') ++p->at;
    else {
        if (*p->at < '1' || *p->at > '9') return 0;
        while (p->at < p->end && *p->at >= '0' && *p->at <= '9') ++p->at;
    }
    if (p->at < p->end && *p->at == '.') {
        ++p->at; if (p->at == p->end || *p->at < '0' || *p->at > '9') return 0;
        while (p->at < p->end && *p->at >= '0' && *p->at <= '9') ++p->at;
    }
    if (p->at < p->end && (*p->at == 'e' || *p->at == 'E')) {
        ++p->at;
        if (p->at < p->end && (*p->at == '+' || *p->at == '-')) ++p->at;
        if (p->at == p->end || *p->at < '0' || *p->at > '9') return 0;
        while (p->at < p->end && *p->at >= '0' && *p->at <= '9') ++p->at;
    }
    return 1;
}
int xf_json_string(const char *json, size_t length, const char *path, char *out, size_t cap) {
    parser p;
    if (!json || !path || !out || !cap || length > XF_BODY_LIMIT) return -1;
    out[0] = 0; p.at = (const unsigned char *)json; p.end = p.at + length;
    p.out = out; p.capacity = cap; p.found = 0;
    space(&p);
    if (p.at == p.end || *p.at != '{' || !value(&p, path, 0)) goto fail;
    space(&p); if (p.at != p.end) goto fail;
    return p.found;
fail:
    xf_wipe(out, cap); return -1;
}
