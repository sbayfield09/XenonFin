#include "sound_host.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,writes,flushes,resets,hdmi_writes,force_empty;
static uint32_t regs[4];static unsigned char *pcm;static uint32_t *descriptors;
int xenos_is_hdmi=1;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"native sound line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned index_of(uintptr_t a){CHECK(a>=0xea001600U && a<=0xea00160cU && !(a&3));return (unsigned)(a-0xea001600U)/4;}
uint32_t read32(uintptr_t a){unsigned n=index_of(a);if(n==1 && force_empty)return (regs[1]&0x1f00U)|((regs[1]>>8)&31);return regs[n];}
void write32(uintptr_t a,uint32_t v){unsigned n=index_of(a);++writes;if(n==1)regs[1]=(regs[1]&~0x1f00U)|(v&0x1f00U);else regs[n]=v;if(n==2 && !v)++resets;}
void memdcbst(void *p,int n){CHECK(p && n>0);if(n==65536){CHECK(!(regs[2]&0x1000000));pcm=p;}++flushes;}
void memdcbf(void *p,int n){CHECK(n==256 && !((uintptr_t)p&255));descriptors=p;}
void xenon_gpio_control(int a,int b,int c){(void)a;(void)b;(void)c;}
int xenon_smc_i2c_write(int a,int b){(void)a;(void)b;++hdmi_writes;return 0;}
void xenon_smc_send_message(const unsigned char *p){CHECK(p[0]==0x8d && p[1]==1 && p[2]==1);}
int main(void){
    static unsigned char bytes[2048];unsigned i,k,previous;uint32_t addresses[]={0xa0000,0x1958000,0x1d0c700};
    memset(bytes,0x37,sizeof(bytes));xenon_sound_init();CHECK(pcm && descriptors && !((uintptr_t)pcm&255) && hdmi_writes && regs[2]==0x1d08001c);
    for(i=0;i<32;++i){CHECK(__builtin_bswap32(descriptors[2*i])==(((uintptr_t)pcm&0x1fffffff)+i*2048));CHECK(__builtin_bswap32(descriptors[2*i+1])==0x80000800U);}
    for(k=0;k<3;++k){
        xenon_sound_init();regs[0]=addresses[k];previous=flushes;
        xenon_sound_submit(bytes,sizeof(bytes));
        CHECK(regs[2]==(0x1d08001cU|0x1000000U));CHECK(flushes>previous && !memcmp(pcm,bytes,sizeof(bytes)));
    }
    xenon_sound_init();for(i=0;i<36;++i)xenon_sound_submit(bytes,sizeof(bytes));CHECK(((regs[1]>>8)&31)==3);
    regs[1]=(2048U<<16)|(3U<<8);CHECK(xenon_sound_get_unplayed()==8192);
    regs[1]=(1024U<<16)|(3U<<8);CHECK(xenon_sound_get_unplayed()==7168);
    regs[1]=(1024U<<16)|(1U<<8)|30;CHECK(xenon_sound_get_unplayed()==7168);
    /* The legacy tone also uses whole descriptors and zero final padding. */
    xenon_sound_init();force_empty=1;xenon_tone(440,200,3000);force_empty=0;
    CHECK(((regs[1]>>8)&31)==18);
    for(i=0;i<9600;++i){int16_t s=(int16_t)(pcm[i*4]|((unsigned)pcm[i*4+1]<<8));CHECK((s==3000 || s==-3000) && pcm[i*4]==pcm[i*4+2] && pcm[i*4+1]==pcm[i*4+3]);}
    for(i=38400;i<38912;++i)CHECK(!pcm[i]);
    previous=hdmi_writes;xenos_is_hdmi=0;xenon_sound_init();CHECK(hdmi_writes==previous && resets>=6);
    printf("Real sound driver control-register preservation, PCM/cache/descriptors/wrap/tone: %u checks PASS\n",checks);return 0;
}
