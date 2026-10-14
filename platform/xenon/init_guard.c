/* Bound the old driver's MDIO busy loops without replacing its init sequence.
 * The pinned enet.o calls mdelay on every PHY busy iteration. Link --wrap routes
 * those calls here; unrelated delays pass through unchanged. */
#include "driver_guard.h"
#include <setjmp.h>
#include <stdint.h>

extern err_t enet_init(struct netif *);
extern void __real_mdelay(int);
static jmp_buf escape_init;
static int armed, expired;
static uint32_t started;

int xf_phy_timed_out(void) { return expired; }
err_t xf_guarded_enet_init(struct netif *interface) {
    err_t result;
    expired = 0;
    if (setjmp(escape_init)) { armed = 0; expired = 1; return ERR_TIMEOUT; }
    started = xf_monotonic_ms(); armed = 1;
    result = enet_init(interface);
    armed = 0;
    return result;
}
void __wrap_mdelay(int milliseconds) {
    if (!armed) { __real_mdelay(milliseconds); return; }
    while (milliseconds > 0) {
        int amount = milliseconds > 10 ? 10 : milliseconds;
        if ((uint32_t)(xf_monotonic_ms() - started) >= 12000) longjmp(escape_init, 1);
        __real_mdelay(amount); milliseconds -= amount;
    }
    if ((uint32_t)(xf_monotonic_ms() - started) >= 12000) longjmp(escape_init, 1);
}
