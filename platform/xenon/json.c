#include "protocol.h"
#include <string.h>

typedef struct {
    const unsigned char *at, *end;
    char *out;
    size_t capacity;
    int found;
    int truncate;
    xf_json_ref *span;
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
static int append(char *out, size_t cap, size_t *used, unsigned char c, int truncate) {
    if (out) {
        if (*used + 1 >= cap) { if (!truncate) return 0; }
        else out[*used] = (char)c;
    }
    ++*used; return 1;
}
static int string(parser *p, char *out, size_t cap) {
    size_t used = 0;
    if (p->at == p->end || *p->at++ != '"') return 0;
    while (p->at < p->end) {
        unsigned c = *p->at++;
        if (c == '"') {
            if (out) {
                size_t end = used < cap ? used : cap - 1, first = end;
                if (end < used) {
                    while (first && ((unsigned char)out[first - 1] & 0xc0) == 0x80) --first;
                    if (first && (unsigned char)out[first - 1] >= 0xc0) {
                        unsigned char lead = (unsigned char)out[first - 1];
                        size_t bytes = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
                        if (end - (first - 1) < bytes) end = first - 1;
                    }
                }
                out[end] = 0;
            }
            return 1;
        }
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
                if (c < 128) { if (!append(out, cap, &used, (unsigned char)c, p->truncate)) return 0; }
                else {
                    if (c > 0xffff && !append(out, cap, &used, (unsigned char)(0xf0 | (c >> 18)), p->truncate)) return 0;
                    if (c > 0x7ff && !append(out, cap, &used, (unsigned char)((c > 0xffff ? 0x80 : 0xe0) | ((c >> 12) & (c > 0xffff ? 63 : 15))), p->truncate)) return 0;
                    if (!append(out, cap, &used, (unsigned char)((c > 0x7ff ? 0x80 : 0xc0) | ((c >> 6) & (c > 0x7ff ? 63 : 31))), p->truncate) ||
                        !append(out, cap, &used, (unsigned char)(0x80 | (c & 63)), p->truncate)) return 0;
                }
                continue;
            }
            { const char *at = strchr(escape, (int)c); if (!at) return 0; c = (unsigned char)value[at - escape]; }
        } else if (c >= 128) {
            unsigned count = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
            unsigned original = c, code = c & (count == 3 ? 7 : count == 2 ? 15 : 31), i;
            if (c < 0xc2 || c > 0xf4 || p->end - p->at < (int)count || !append(out, cap, &used, (unsigned char)c, p->truncate)) return 0;
            for (i = 0; i < count; ++i) {
                unsigned next = *p->at++;
                if ((next & 0xc0) != 0x80 || !append(out, cap, &used, (unsigned char)next, p->truncate)) return 0;
                code = (code << 6) | (next & 63);
            }
            if ((original >= 0xe0 && code < 0x800) || (original >= 0xf0 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
            continue;
        }
        if (!append(out, cap, &used, (unsigned char)c, p->truncate)) return 0;
    }
    return 0;
}
static int value(parser *p, const char *path, unsigned depth) {
    unsigned char c;
    if (depth > 24) return 0;
    space(p); if (p->at == p->end) return 0; c = *p->at;
    if (path && !*path) {
        if (p->span) {
            const unsigned char *begin = p->at;
            if (p->found || !value(p, NULL, depth)) return 0;
            p->span->data = (const char *)begin; p->span->size = (size_t)(p->at - begin);
            p->found = 1; return 1;
        }
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
    memset(&p, 0, sizeof(p));
    out[0] = 0; p.at = (const unsigned char *)json; p.end = p.at + length;
    p.out = out; p.capacity = cap; p.found = 0;
    space(&p);
    if (p.at == p.end || *p.at != '{' || !value(&p, path, 0)) goto fail;
    space(&p); if (p.at != p.end) goto fail;
    return p.found;
fail:
    xf_wipe(out, cap); return -1;
}
int xf_json_get(xf_json_ref input, const char *path, xf_json_ref *out) {
    parser p;
    if (!input.data || input.size > XF_BODY_LIMIT || !path || !out) return -1;
    memset(&p, 0, sizeof(p)); memset(out, 0, sizeof(*out));
    p.at = (const unsigned char *)input.data; p.end = p.at + input.size; p.span = out;
    space(&p);
    if (p.at == p.end || *p.at != '{' || !value(&p, path, 0)) return -1;
    space(&p); return p.at == p.end ? p.found : -1;
}
int xf_json_elements(xf_json_ref input, size_t start, xf_json_ref *out, size_t cap, size_t *count) {
    parser p; size_t n = 0;
    if (!input.data || input.size > XF_BODY_LIMIT || !count || (cap && !out)) return 0;
    memset(&p, 0, sizeof(p)); p.at = (const unsigned char *)input.data; p.end = p.at + input.size;
    space(&p); if (p.at == p.end || *p.at++ != '[') return 0;
    space(&p);
    if (p.at < p.end && *p.at == ']') ++p.at;
    else for (;;) {
        const unsigned char *begin = p.at;
        if (!value(&p, NULL, 1)) return 0;
        if (n >= start && n - start < cap) {
            out[n - start].data = (const char *)begin; out[n - start].size = (size_t)(p.at - begin);
        }
        ++n; space(&p); if (p.at == p.end) return 0;
        if (*p.at == ']') { ++p.at; break; }
        if (*p.at++ != ',') return 0;
        space(&p);
    }
    space(&p); *count = n; return p.at == p.end;
}
int xf_json_text(xf_json_ref input, char *out, size_t cap) {
    parser p;
    if (!input.data || input.size > XF_BODY_LIMIT || !out || !cap) return 0;
    memset(&p, 0, sizeof(p)); p.at = (const unsigned char *)input.data; p.end = p.at + input.size; p.truncate = 1;
    out[0] = 0;
    if (!string(&p, out, cap) || p.at != p.end) { out[0] = 0; return 0; }
    return 1;
}
int xf_json_uint(xf_json_ref input, uint64_t *out) {
    size_t i; uint64_t n = 0;
    if (!input.data || !input.size || input.size > 20 || !out) return 0;
    if (input.size > 1 && input.data[0] == '0') return 0;
    for (i = 0; i < input.size; ++i) {
        unsigned digit = (unsigned char)input.data[i] - '0';
        if (digit > 9 || n > (UINT64_MAX - digit) / 10) return 0;
        n = n * 10 + digit;
    }
    *out = n; return 1;
}
int xf_json_bool(xf_json_ref input, int *out) {
    if (!input.data || !out) return 0;
    if (input.size == 4 && !memcmp(input.data, "true", 4)) { *out = 1; return 1; }
    if (input.size == 5 && !memcmp(input.data, "false", 5)) { *out = 0; return 1; }
    return 0;
}
