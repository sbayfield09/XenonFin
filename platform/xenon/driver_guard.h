#ifndef XF_DRIVER_GUARD_H
#define XF_DRIVER_GUARD_H
#include <stdint.h>
#include <lwip/netif.h>
/* Adapter for the pinned Free60 enet.o, not a new Ethernet implementation. */
void xf_driver_guard_install(struct netif *);
unsigned xf_driver_busy_count(void);
err_t xf_guarded_enet_init(struct netif *);
int xf_phy_timed_out(void);
uint32_t xf_monotonic_ms(void);
#endif
