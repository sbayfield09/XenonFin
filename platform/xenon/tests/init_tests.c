#include "../driver_guard.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static uint32_t now;
static unsigned delays,checks;
static int stuck;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"PHY guard line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void __wrap_mdelay(int);
uint32_t xf_monotonic_ms(void){return now;}
void __real_mdelay(int milliseconds){CHECK(milliseconds>0);now+=(uint32_t)milliseconds;++delays;}
err_t enet_init(struct netif *n){(void)n;if(stuck){for(;;)__wrap_mdelay(1);}__wrap_mdelay(500);return ERR_OK;}
int main(void){struct netif n;
 CHECK(xf_guarded_enet_init(&n)==ERR_OK&&!xf_phy_timed_out()&&now==500&&delays==50);
 stuck=1;CHECK(xf_guarded_enet_init(&n)==ERR_TIMEOUT&&xf_phy_timed_out()&&now==12500);
 now=UINT32_MAX-100;CHECK(xf_guarded_enet_init(&n)==ERR_TIMEOUT&&xf_phy_timed_out()&&now==(uint32_t)(UINT32_MAX-100+12000u));
 {unsigned before=delays;__wrap_mdelay(500);CHECK(delays==before+1);}
 printf("PHY initialization watchdog/wraparound: %u checks passed\n",checks);return 0;}
