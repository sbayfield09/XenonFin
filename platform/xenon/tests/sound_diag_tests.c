#include "../sound_diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static xf_sound_diag d;static unsigned checks,pending,written;static unsigned char bytes[65536];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"sound diag line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int queued(void *p){(void)p;return (int)pending;}
static int available(void *p){(void)p;return 65536-(int)pending;}
static void submit(void *p,const unsigned char *data,size_t n){(void)p;CHECK(n==2048 && pending+n<=8192 && written+n<=sizeof(bytes));memcpy(bytes+written,data,n);written+=(unsigned)n;pending+=(unsigned)n;}
int main(void){
    xf_audio_backend b={queued,available,submit,NULL};uint32_t now=0;unsigned i;
    xf_sound_diag_init(&d,&b);CHECK(!xf_sound_diag_start(&d,1,now) && !written);
    pending=4096;CHECK(!xf_sound_diag_start(&d,0,now) && !written && strstr(d.message,"DMA"));pending=0;
    CHECK(xf_sound_diag_start(&d,0,now));
    while(xf_sound_diag_active(&d) && now<1000){xf_sound_diag_tick(&d,now);CHECK(pending<=8192);pending=pending>1920?pending-1920:0;now+=10;}
    CHECK(d.state==XF_TONE_DONE && d.frames==9600 && written==38912 && strstr(d.message,"Confirm audible"));
    for(i=38400;i<written;++i)CHECK(!bytes[i]);
    written=pending=0;CHECK(xf_sound_diag_start(&d,0,now));xf_sound_diag_tick(&d,now);CHECK(pending==8192);
    xf_sound_diag_tick(&d,now+2000);CHECK(d.state==XF_TONE_ERROR && strstr(d.message,"no progress"));
    pending=65537;CHECK(!xf_sound_diag_start(&d,0,now));
    printf("Cooperative 440 Hz request/ownership, whole descriptors, padding and no-progress timeout: %u checks PASS\n",checks);return 0;
}
