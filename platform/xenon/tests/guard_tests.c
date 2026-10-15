#include "../driver_guard.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* Host-sized mirror of the pinned driver's prefix for adapter branch tests. */
struct prefix {volatile uint32_t *rx;void *rx_buffer;int rx_index;volatile uint32_t *tx;int tx_index;void *tx_buffer;};
static unsigned calls,checks,invalidations;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"TX guard line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void memdcbf(void *p,int len){CHECK(p&&len==16);++invalidations;}
static err_t original(struct netif *n,struct pbuf *p){CHECK(n&&p);++calls;return ERR_OK;}
int main(void){uint32_t descriptors[64]={0};struct prefix context;struct netif n;struct pbuf packet;
 memset(&context,0,sizeof(context));memset(&n,0,sizeof(n));memset(&packet,0,sizeof(packet));context.tx=descriptors;n.state=&context;n.linkoutput=original;packet.tot_len=128;
 xf_driver_guard_install(&n);xf_driver_guard_install(&n);CHECK(n.linkoutput(&n,&packet)==ERR_OK&&calls==1);
 descriptors[1]=__builtin_bswap32(0x80000000u);CHECK(n.linkoutput(&n,&packet)==ERR_MEM&&calls==1&&xf_driver_busy_count()==1);
 descriptors[1]=0;CHECK(n.linkoutput(&n,&packet)==ERR_OK&&calls==2);
 context.tx_index=16;CHECK(n.linkoutput(&n,&packet)==ERR_IF&&calls==2);context.tx_index=0;
 packet.tot_len=1529;CHECK(n.linkoutput(&n,&packet)==ERR_BUF&&calls==2);CHECK(invalidations==3);
 context.rx=descriptors;context.rx_index=3;descriptors[13]=__builtin_bswap32(0x80000000u);
 CHECK(!xf_driver_rx_ready(&n));descriptors[13]=0;CHECK(xf_driver_rx_ready(&n));
 context.rx_index=16;CHECK(!xf_driver_rx_ready(&n));context.rx_index=-1;CHECK(!xf_driver_rx_ready(&n));
 context.rx_index=0;context.rx=NULL;CHECK(!xf_driver_rx_ready(&n));CHECK(!xf_driver_rx_ready(NULL));
 printf("Pinned Ethernet TX guard and RX descriptor inspection: %u checks passed\n",checks);return 0;}
