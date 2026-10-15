/* Capture existing printf-based pages into a complete character frame. Never
 * redirect libXenon's private console_fb or suppress audio/NIC cache stores. */
#include "console_batch.h"
#include "framebuffer.h"
#include <console/console.h>
#include <stdio.h>
#include <stdarg.h>
extern void (*stdout_hook)(const char *, int);
extern uint32_t xf_monotonic_ms(void);
static void (*boot_hook)(const char *, int);
static xf_console_frame frame;
static void capture(const char *text, int size) {
    while (size-- > 0) xf_console_putch((unsigned char)*text++);
}
int xf_console_init(void) {
    xf_console_backend backend;
    unsigned columns, rows, old_columns, old_rows;
    if (!xf_framebuffer_init(&backend, &columns, &rows)) return 0;
    console_get_dimensions(&old_columns, &old_rows);
    if (columns != old_columns || rows != old_rows || !xf_console_frame_init(&frame, columns, rows, &backend)) return 0;
    /* printf fragments and direct glyph calls must retain their order. */
    setvbuf(stdout, NULL, _IONBF, 0);
    boot_hook = stdout_hook;
    /* The boot console now IS the displayed surface. Outside UI capture,
     * leave serial logging to vfs_console_write without letting its console
     * hook scribble over text whose cached cells are otherwise unchanged. */
    stdout_hook = NULL; return 1;
}
int xf_console_ready(void) { return xf_console_frame_ready(&frame); }
void xf_console_poll(uint32_t now) { xf_console_frame_poll(&frame, now); }
void xf_console_begin(void) {
    fflush(stdout);
    if (xf_console_frame_begin(&frame, console_color[0], console_color[1])) stdout_hook = capture;
}
void xf_console_putch(unsigned char c) { xf_console_frame_putch(&frame, c, console_color[0], console_color[1]); }
int xf_console_cursor_y(void) { return (int)frame.y; }
void xf_console_end(void) {
    fflush(stdout); stdout_hook = NULL;
    xf_console_frame_end(&frame, xf_monotonic_ms());
}
void xf_console_shutdown(void) { stdout_hook = boot_hook; xf_framebuffer_shutdown(); }

/* stdout_hook only captures the framebuffer side effect: upstream newlib
 * ALSO sends each byte to blocking UART putch(). Bypass VFS for UI text.
 * Keep ordinary vprintf (and driver serial logs) outside composed frames. */
int xf_console_printf(const char *format, ...) {
    va_list args; int length;
    va_start(args,format);
    if(frame.active) {
        char text[2048];
        length=vsnprintf(text,sizeof(text),format,args);
        if(length>0) capture(text,length<(int)sizeof(text)?length:(int)sizeof(text)-1);
    } else length=vprintf(format,args);
    va_end(args); return length;
}
