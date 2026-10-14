#include "text.h"
#include <string.h>
static unsigned char next(const char *s, size_t *at) {
    unsigned char c = (unsigned char)s[(*at)++];
    if (c >= 128) {
        while (((unsigned char)s[*at] & 0xc0) == 0x80) ++*at;
        return '?';
    }
    return c >= 32 && c < 127 ? c : ' ';
}
void xf_text_cell(char *out, size_t cap, const char *text, unsigned width) {
    size_t at = 0; unsigned used = 0;
    if (!cap) return;
    if (width >= cap) width = (unsigned)cap - 1;
    while (text[at] && used < width) out[used++] = (char)next(text, &at);
    if (text[at] && width >= 3) memset(out + width - 3, '.', 3);
    while (used < width) out[used++] = ' ';
    out[used] = 0;
}
int xf_text_line(const char *text, size_t *offset, unsigned width, char *out, size_t cap) {
    size_t at = *offset, space_at = 0; unsigned used = 0, space_column = 0;
    if (!cap || !width) return 0;
    if (width >= cap) width = (unsigned)cap - 1;
    while (text[at] == ' ' || text[at] == '\t' || text[at] == '\r') ++at;
    if (!text[at]) { out[0] = 0; *offset = at; return 0; }
    while (text[at] && text[at] != '\n' && used < width) {
        if (text[at] == ' ' || text[at] == '\t') { space_at = at + 1; space_column = used; }
        out[used++] = (char)next(text, &at);
    }
    if (used == width && text[at] && text[at] != '\n' && text[at] != ' ' && space_at) {
        at = space_at; used = space_column;
    }
    while (used && out[used - 1] == ' ') --used;
    out[used] = 0;
    if (text[at] == '\n') ++at;
    *offset = at; return 1;
}
unsigned xf_text_lines(const char *s, unsigned width) {
    char line[161]; size_t at = 0; unsigned n = 0;
    while (xf_text_line(s, &at, width, line, sizeof(line))) ++n;
    return n;
}
