#include "driver_guard.h"
#include <ppc/cache.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

/* The pinned enet.o's private context prefix, verified against both source and
 * PowerPC disassembly: TX descriptor pointer +12, TX slot index +16. Revalidate
 * this adapter if changing the toolchain/library image. The driver is untouched. */
typedef struct {
    volatile uint32_t *rx_descriptors;
    void *rx_buffer;
    int rx_index;
    volatile uint32_t *tx_descriptors;
    int tx_index;
    void *tx_buffer;
} enet_prefix;
#ifdef XENON
typedef char assert_tx_pointer_offset[(offsetof(enet_prefix, tx_descriptors) == 12) ? 1 : -1];
typedef char assert_tx_index_offset[(offsetof(enet_prefix, tx_index) == 16) ? 1 : -1];
#endif
static netif_linkoutput_fn original;
static unsigned busy;
static err_t send_guarded(struct netif *interface, struct pbuf *packet) {
    enet_prefix snapshot;
    volatile uint32_t *descriptor;
    if (!original || !interface->state || !packet) return ERR_IF;
    if (packet->tot_len > 1528) return ERR_BUF; /* Original driver's TX slot size. */
    memcpy(&snapshot, interface->state, sizeof(snapshot));
    if (!snapshot.tx_descriptors || snapshot.tx_index < 0 || snapshot.tx_index >= 16) return ERR_IF;
    descriptor = snapshot.tx_descriptors + snapshot.tx_index * 4;
    memdcbf((void *)descriptor, 16);
    if (__builtin_bswap32(descriptor[1]) & 0x80000000u) {
        ++busy; return ERR_MEM;
    }
    /* NO_SYS: no other software sender can take this slot between the check and
     * call. Hardware only clears ownership. This also avoids overwriting an
     * in-flight DMA buffer before the original driver's busy wait. */
    return original(interface, packet);
}
void xf_driver_guard_install(struct netif *interface) {
    if (interface->linkoutput != send_guarded) { original = interface->linkoutput; interface->linkoutput = send_guarded; }
}
unsigned xf_driver_busy_count(void) { return busy; }
