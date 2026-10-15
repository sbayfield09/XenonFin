#ifndef XF_CONSOLE_BATCH_H
#define XF_CONSOLE_BATCH_H
#include <stdint.h>
int xf_console_init(void);
int xf_console_printf(const char *, ...) __attribute__((format(printf,1,2)));
int xf_console_ready(void);
void xf_console_poll(uint32_t now);
void xf_console_begin(void);
void xf_console_putch(unsigned char);
int xf_console_cursor_y(void);
void xf_console_end(void);
void xf_console_shutdown(void);
#endif
