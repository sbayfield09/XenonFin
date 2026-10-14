#ifndef XF_TEXT_H
#define XF_TEXT_H
#include <stddef.h>
/* Printable ASCII console cells; one '?' per non-ASCII codepoint. */
void xf_text_cell(char *, size_t, const char *, unsigned width);
/* Word-wrapped line iterator. No terminal controls reach the console. */
int xf_text_line(const char *, size_t *offset, unsigned width, char *, size_t);
unsigned xf_text_lines(const char *, unsigned width);
#endif
