/* Exercise the native stdout-hook/direct-character bridge using the same
 * vfs_console_write hook calls as newlib, without a hosted printf substitute. */
#include "../console_batch.h"
#include "../framebuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, boot_writes, shutdowns;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"adapter line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
uint32_t console_color[2] = {1,2};
static unsigned char cells[2][32];
static int front = -1, pending = -1;
static uint32_t now;
static void boot(const char *text, int size) { (void)text; (void)size; ++boot_writes; }
void (*stdout_hook)(const char *, int) = boot;
uint32_t xf_monotonic_ms(void) { return now; }
void console_get_dimensions(unsigned *columns, unsigned *rows) { *columns=8; *rows=4; }
static void clear(void *c, unsigned b, uint32_t color) { (void)c; (void)color; CHECK((int)b!=front); memset(cells[b],' ',32); }
static void glyph(void *c, unsigned b, unsigned x, unsigned y, const xf_console_cell *cell) {
    (void)c; CHECK((int)b!=front); cells[b][y*8+x]=cell->character;
}
static void flush(void *c, unsigned b) { (void)c; CHECK((int)b!=front); }
static int flip(void *c, unsigned b) { (void)c; pending=(int)b; return 1; }
static int complete(void *c, unsigned b) { (void)c; CHECK(pending==(int)b); front=pending; pending=-1; return 1; }
int xf_framebuffer_init(xf_console_backend *backend, unsigned *columns, unsigned *rows) {
    xf_console_backend ops = {clear,glyph,flush,flip,complete,NULL};
    *backend=ops; *columns=8; *rows=4; return 1;
}
void xf_framebuffer_shutdown(void) { ++shutdowns; }
int main(void) {
    CHECK(xf_console_init()); CHECK(stdout_hook==boot && xf_console_ready());
    xf_console_begin(); CHECK(stdout_hook!=boot); stdout_hook("Title: ",7); xf_console_putch('A');
    CHECK(xf_console_cursor_y()==1); stdout_hook("Track",5); xf_console_end();
    CHECK(stdout_hook==boot && !xf_console_ready()); CHECK(!memcmp(cells[pending],"Title: ATrack",13));
    stdout_hook("background driver log",21); CHECK(boot_writes==1);
    xf_console_poll(++now); CHECK(xf_console_ready());
    xf_console_begin(); stdout_hook("Title: B\nTrack",14); xf_console_end();
    CHECK(front==0 && !memcmp(cells[front],"Title: ATrack",13));
    xf_console_poll(++now); CHECK(front==1 && !memcmp(cells[front],"Title: B        Track",21));
    xf_console_shutdown(); CHECK(shutdowns==1 && stdout_hook==boot);
    printf("Native stdout capture, direct glyph ordering and boot-console isolation: %u assertions passed\n",checks);
    return 0;
}
