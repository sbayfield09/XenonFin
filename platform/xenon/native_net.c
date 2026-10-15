#include "native_net.h"
#include "driver_guard.h"
#include <network/network.h>
#include <lwip/init.h>
#include <lwip/ip.h>
#include <lwip/ip_frag.h>
#include <netif/etharp.h>
#include <stdio.h>
#include <string.h>

/* These are the same existing Ethernet driver and global interface used by
 * network_init/network_poll. We do not replace or modify the working driver. */
extern void enet_quiesce(void);
static int initialized, broken, waiting, timed_out, start_error;
static uint32_t began, arp_tick, fragment_tick;
extern uint64_t xf_monotonic_us(void);
static xf_network_metrics metrics;
static uint64_t polled_at;
const xf_network_metrics *xf_network_measurements(void) {return &metrics;}

int xf_network_available(void) { return initialized; }
int xf_network_ready(void) {
    struct dhcp *d = initialized ? netif.dhcp : NULL;
    return d && network_link_up() && xf_lease_valid(netif.flags & NETIF_FLAG_DHCP, d->state,
        netif_is_up(&netif), netif.ip_addr.addr, d->offered_ip_addr.addr, d->offered_t0_lease);
}
int xf_network_start(uint32_t now) {
    ip_addr_t zero;
    if (broken) return 0;
    IP4_ADDR(&zero, 0, 0, 0, 0);
    if (!initialized) {
        lwip_init();
        if (!netif_add(&netif, &zero, &zero, &zero, NULL, xf_guarded_enet_init, ip_input)) {
            if (xf_phy_timed_out()) enet_quiesce(); /* Disable RX/TX after partial hardware init. */
            broken = 1; return 0;
        }
        xf_driver_guard_install(&netif);
        netif_set_default(&netif); initialized = 1; arp_tick = fragment_tick = now;
    } else dhcp_stop(&netif);
    netif_set_addr(&netif, &zero, &zero, &zero);
    /* PHY startup may have consumed time. DHCP gets its own 20-second window. */
    began = xf_monotonic_ms(); waiting = 1; timed_out = 0;
    start_error = dhcp_start(&netif);
    if (start_error != ERR_OK) waiting = 0;
    return start_error == ERR_OK;
}
void xf_network_tick(uint32_t now) {
    if (!initialized) return;
    /* Pinned network_poll -> enet_poll consumes ONE Ethernet descriptor, not
     * the RX ring. Drain pending packets without waiting for new arrivals.
     * At most one 16-slot ring or 2ms per service; audio runs on either side.
     * Always call once for DHCP/TCP timers, including an empty RX ring. */
    {
        uint64_t start=xf_monotonic_us();unsigned frames=0,attempts=0;
        if(polled_at && start-polled_at>metrics.max_gap_us)metrics.max_gap_us=start-polled_at;
        polled_at=start;
        do {
            int pending=xf_driver_rx_ready(&netif);
            network_poll();++metrics.polls;++attempts;
            if(pending)++frames;
            if(!xf_driver_rx_ready(&netif))break;
            if(attempts>=16 || xf_monotonic_us()-start>=2000) {++metrics.budget_yields;break;}
        } while(1);
        metrics.frames+=frames;metrics.last_batch=frames;
        if(frames>metrics.max_batch)metrics.max_batch=frames;
    }
    if ((uint32_t)(now - arp_tick) >= ARP_TMR_INTERVAL) { etharp_tmr(); arp_tick = now; }
#if IP_REASSEMBLY
    if ((uint32_t)(now - fragment_tick) >= IP_TMR_INTERVAL) { ip_reass_tmr(); fragment_tick = now; }
#endif
    if (xf_network_ready()) { waiting = 0; timed_out = 0; }
    else if (waiting && (uint32_t)(now - began) >= 20000) { waiting = 0; timed_out = 1; }
}
const char *xf_network_status(void) {
    if (broken) return xf_phy_timed_out() ? "PHY/MDIO init timed out (12s); reboot before retry" : "NIC initialization failed; reboot to retry";
    if (!initialized) return "Not initialized: press A on Network";
    if (xf_network_ready()) return "DHCP lease verified (ACK bound address)";
    if (start_error) return "dhcp_start failed; press A to retry";
    if (timed_out) return "DHCP timed out (20s); no static-IP fallback. A retry";
    return "DHCP pending; no verified lease yet";
}
const char *xf_dhcp_state(void) {
    struct dhcp *d = initialized ? netif.dhcp : NULL;
    if (!d) return "OFF / no DHCP client";
    switch (d->state) {
    case DHCP_OFF: return "OFF"; case DHCP_REQUESTING: return "REQUESTING";
    case DHCP_INIT: return "INIT"; case DHCP_REBOOTING: return "REBOOTING";
    case DHCP_REBINDING: return "REBINDING (retaining lease)";
    case DHCP_RENEWING: return "RENEWING (retaining lease)";
    case DHCP_SELECTING: return "SELECTING"; case DHCP_INFORMING: return "INFORMING";
    case DHCP_CHECKING: return "CHECKING (ARP conflict test)";
    case DHCP_BOUND: return "BOUND (DHCP ACK accepted)";
    case DHCP_BACKING_OFF: return "BACKING OFF"; default: return "UNKNOWN";
    }
}
