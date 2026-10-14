/* Host state-machine tests only; no claim that a host fake obtained DHCP. */
#include "../native_net.h"
#include <network/network.h>
#include <lwip/init.h>
#include <lwip/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct netif netif;
static struct dhcp lease;
static unsigned checks, inits, starts, stops, polls, arps, fragments, guards;
static uint32_t clock_value;
static unsigned quiesces;
static int fail_init;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"DHCP adapter line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void lwip_init(void){++inits;}
err_t enet_init(struct netif *n){n->flags=NETIF_FLAG_ETHARP|NETIF_FLAG_LINK_UP;return ERR_OK;}
err_t xf_guarded_enet_init(struct netif *n){return fail_init?ERR_TIMEOUT:enet_init(n);}
int xf_phy_timed_out(void){return fail_init;}
uint32_t xf_monotonic_ms(void){return clock_value;}
struct netif *netif_add(struct netif *n,ip_addr_t *ip,ip_addr_t *mask,ip_addr_t *gw,void *state,netif_init_fn init,netif_input_fn input){(void)state;n->ip_addr=*ip;n->netmask=*mask;n->gw=*gw;n->input=input;CHECK(init);if(init(n)!=ERR_OK)return NULL;return n;}
void netif_set_default(struct netif *n){CHECK(n==&netif);}
void netif_set_addr(struct netif *n,ip_addr_t *ip,ip_addr_t *mask,ip_addr_t *gw){n->ip_addr=*ip;n->netmask=*mask;n->gw=*gw;}
err_t dhcp_start(struct netif *n){++starts;CHECK(n->ip_addr.addr==0);memset(&lease,0,sizeof(lease));n->dhcp=&lease;n->flags|=NETIF_FLAG_DHCP;lease.state=DHCP_SELECTING;return ERR_OK;}
void dhcp_stop(struct netif *n){++stops;lease.state=DHCP_OFF;n->flags&=(u8_t)~NETIF_FLAG_DHCP;}
void network_poll(void){++polls;}
void enet_quiesce(void){++quiesces;}
void etharp_tmr(void){++arps;}
void ip_reass_tmr(void){++fragments;}
err_t ip_input(struct pbuf *p,struct netif *n){(void)p;(void)n;return ERR_OK;}
void xf_driver_guard_install(struct netif *n){CHECK(n==&netif);++guards;}
int main(int argc,char **argv){
 (void)argv;
 if(argc>1){fail_init=1;CHECK(!xf_network_start(0));CHECK(!xf_network_available()&&quiesces==1&&strstr(xf_network_status(),"12s"));CHECK(!xf_network_start(20000)&&inits==1&&quiesces==1);puts("PHY timeout adapter: RX/TX quiesced; reboot required; no unsafe retry PASS");return 0;}
 CHECK(!xf_network_available()&&!xf_network_ready());CHECK(xf_network_start(0));CHECK(inits==1&&starts==1&&guards==1);
 CHECK(!xf_network_ready()&&strstr(xf_dhcp_state(),"SELECTING"));xf_network_tick(1000);CHECK(polls==1&&fragments==1);
 xf_network_tick(5000);CHECK(arps==1);xf_network_tick(20000);CHECK(strstr(xf_network_status(),"timed out")&&!netif.ip_addr.addr);
 /* A displayed nonzero address with a pending DHCP client is not success. */
 netif.ip_addr.addr=123;netif.flags|=NETIF_FLAG_UP;CHECK(!xf_network_ready());
 lease.state=DHCP_BOUND;lease.offered_ip_addr.addr=123;lease.offered_t0_lease=3600;CHECK(xf_network_ready());xf_network_tick(21000);CHECK(strstr(xf_network_status(),"verified"));
 lease.state=DHCP_RENEWING;CHECK(xf_network_ready());lease.offered_ip_addr.addr=124;CHECK(!xf_network_ready());lease.offered_ip_addr.addr=123;
 netif.flags&=(u8_t)~NETIF_FLAG_LINK_UP;CHECK(!xf_network_ready());netif.flags|=NETIF_FLAG_LINK_UP;
 clock_value=22000;CHECK(xf_network_start(22000));CHECK(inits==1&&starts==2&&stops==1&&guards==1&&!netif.ip_addr.addr&&!xf_network_ready());
 xf_network_tick(23000);CHECK(polls>=5);printf("DHCP adapter/start/retry/timers: %u checks passed\n",checks);return 0;}
