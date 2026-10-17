/* Isolated hardware PCM acceptance. No HTTP/lwIP, MP3/MPEG, player or GPU video
 * renderer is linked. xenos_init/console are used only for minimal text. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <xenos/xenos.h>
#include <xenon_sound/sound.h>
#include <ppc/timebase.h>
#include <pci/io.h>
#include <time/time.h>
#include "sound_diag.h"
#include "audio_native.h"
#include "console_batch.h"
#include "timing.h"
#define printf xf_console_printf
static xf_sound_diag test;
static unsigned snapshot_page;
static uint64_t clock_us(void) {
    uint32_t high,low,check;
    do {
        asm volatile("mftbu %0":"=r"(high));
        asm volatile("mftbl %0":"=r"(low));
        asm volatile("mftbu %0":"=r"(check));
    } while(high!=check);
    return xf_ticks_to_us(((uint64_t)high<<32)|low,PPC_TIMEBASE_FREQ);
}
uint32_t xf_monotonic_ms(void){return (uint32_t)(clock_us()/1000);}
static void draw(uint64_t now) {
    uint32_t status=read32(0xea001604);uint64_t elapsed;
    extern int xenos_is_hdmi;
    elapsed=test.submitted?((test.finished_at?test.finished_at:now)-test.started_at):0;
    console_set_colors(0xff101624,0xffe6eaf4);xf_console_begin();
    printf("\n  XENONFIN 0.7.5 - ISOLATED PCM TEST\n\n");
    printf("  Unmodified pinned libXenon sound.o | %s\n",xenos_is_hdmi?"HDMI":"analogue");
    {const xf_audio_native_metrics *a=xf_audio_native_status();
     printf("  %s owner %s | LB/RB snapshots %u/3\n",xf_audio_native_state_name(a->state),xf_audio_native_owner_name(a->owner),snapshot_page+1);
     if(snapshot_page) {
        static const char *names[]={"before init","after init","before PCM","playing","stopped","first fault"};unsigned i;
        for(i=(snapshot_page-1)*3;i<snapshot_page*3;++i){const xf_audio_dma_snapshot *r=&a->snapshots[i];
            printf("  %s valid %u base %08x control %08x\n",names[i],r->valid,r->base,r->control);
            printf("  status %08x read %u last %u remain %u queue %u\n",r->status,r->status&31,r->status>>8&31,r->status>>16,r->queued);}
        printf("  Init %u duplicate %u active %u conflicts %u shutdown %u\n",a->init_calls,a->double_init,a->active_init,a->ownership_conflicts,a->shutdown_errors);
        printf("  First startup wait %08x queue %u observations %u\n",a->startup_sample.status,a->startup_sample.queued,a->startup_observations);
        printf("  %s\n",a->error);xf_console_end();return;
     }
     if(a->error[0])printf("  %.100s\n",a->error);
    }
    printf("  No network, codecs, video player or A/V clock.\n\n");
    printf("  Left/Right: select test   Up/Down: 1 or 60 seconds\n");
    printf("  A: run   X: stop/drain   B: XeLL after drain\n\n");
    printf("  Test %u/7: %s | %us | state %u\n",test.mode+1,xf_sound_diag_name(test.mode),test.seconds,test.state);
    printf("  SDK format: 48000 Hz, signed 16-bit LE, L/R stereo\n");
    printf("  4 bytes/frame; 2048 bytes/DMA descriptor\n");
    printf("  PCM preflight %s hash %08x\n",test.verified?"PASS":"not run",test.pcm_hash);
    printf("  Source frames %u/%u | submitted %u frames\n",test.frames,test.target_frames,test.submitted/4);
    printf("  DMA consumed %u frames, queued %u bytes\n",test.played,test.queued);
    printf("  Elapsed %.3fs | expected submitted %.3fs\n",(double)elapsed/1000000,(double)test.submitted/(48000*4));
    printf("  48000-frame window: %u frames in %.3f ms\n",test.rate_frames,(double)test.rate_us/1000);
    printf("  Estimated consumption %.1f Hz (DMA counter)\n",test.rate_us?(double)test.rate_frames*1000000/test.rate_us:0);
    printf("  Underruns %u counter faults %u | gap %.3f ms\n",test.underruns,test.counter_faults,(double)test.max_service_gap_us/1000);
    printf("  Base %08x control %08x status %08x\n",read32(0xea001600),read32(0xea001608),status);
    printf("  Read %u last-valid %u remaining %u bytes\n",status&31,(status>>8)&31,status>>16);
    printf("\n  %s\n",test.message);
    printf("\n  First acceptance: clean, correctly pitched 440 Hz.\n");
    printf("  Do not advance if distorted. No automatic PASS.\n");
    xf_console_end();
}
int main(void) {
    xf_audio_backend audio;struct controller_data_s pad,previous;
    uint32_t drawn=0;int dirty=1;
    memset(&previous,0,sizeof(previous));
    /* Same hardware-tested initialisation order as the application. */
    xenos_init(VIDEO_MODE_AUTO);console_init();usb_init();xf_audio_native_init(&audio);xf_sound_diag_init(&test,&audio);
    if(!xf_console_init())return 1;
    for(;;) {
        uint64_t now=clock_us();uint32_t ms=(uint32_t)(now/1000);unsigned mode=test.mode,seconds=test.seconds;
        xf_audio_native_service(now);
        xf_sound_diag_tick_us(&test,now);
        usb_do_poll();memset(&pad,0,sizeof(pad));get_controller_data(&pad,0);
        if((pad.lb && !previous.lb)||(pad.rb && !previous.rb)){snapshot_page=(snapshot_page+(pad.rb?1:2))%3;dirty=1;}
        if(!xf_sound_diag_active(&test)) {
            if(pad.right && !previous.right)mode=(mode+1)%XF_SOUND_CASES;
            if(pad.left && !previous.left)mode=(mode+XF_SOUND_CASES-1)%XF_SOUND_CASES;
            if((pad.up && !previous.up)||(pad.down && !previous.down))seconds=seconds==1?60:1;
            if(mode!=test.mode || seconds!=test.seconds){xf_sound_diag_select(&test,mode,seconds);dirty=1;}
            if(pad.a && !previous.a){xf_sound_diag_start(&test,0,ms);dirty=1;}
        }
        if(pad.x && !previous.x){xf_sound_diag_stop(&test);dirty=1;}
        if(pad.b && !previous.b) {
            if(xf_sound_diag_active(&test))xf_sound_diag_stop(&test);
            else if(!xf_audio_native_pending()){xf_console_shutdown();return 0;}
            else snprintf(test.message,sizeof(test.message),"DMA still owns audio. Wait for drain; cold boot if stuck.");
            dirty=1;
        }
        xf_sound_diag_tick_us(&test,clock_us());xf_console_poll(ms);
        if(xf_console_ready() && (dirty || (uint32_t)(ms-drawn)>=250)) {draw(clock_us());drawn=ms;dirty=0;}
        xf_sound_diag_tick_us(&test,clock_us());previous=pad;
        /* Controller loop pacing only; generation/submission never sleeps. */
        udelay(500);
    }
}
