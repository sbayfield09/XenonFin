/* Native boot diagnostic. This target deliberately does not claim to be the
 * desktop Jellyfin client or a working console player. See docs/PLATFORM.md. */
#include <stdio.h>
#include <string.h>
#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <xenos/xenos.h>
#include <xenon_sound/sound.h>
#include <network/network.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <xenon_soc/xenon_power.h>

static const char *sections[]={"Graphics / controller","Network","Audio output","Platform status"};
int main(void) {
    struct controller_data_s pad,previous;
    int selected=0,redraw=1,network_status=-2;
    memset(&previous,0,sizeof(previous));
    xenos_init(VIDEO_MODE_AUTO);
    console_init();
    usb_init();
    xenon_sound_init();
    for(;;) {
        usb_do_poll();
        memset(&pad,0,sizeof(pad));
        get_controller_data(&pad,0);
        if((pad.down && !previous.down)||(pad.rb && !previous.rb)) {selected=(selected+1)%4;redraw=1;}
        if((pad.up && !previous.up)||(pad.lb && !previous.lb)) {selected=(selected+3)%4;redraw=1;}
        if(pad.a && !previous.a) {
            if(selected==1) network_status=network_init();
            if(selected==2) xenon_tone(440,200,XENON_TONE_AMPLITUDE_25);
            redraw=1;
        }
        if(pad.b && !previous.b) return 0;
        if(redraw) {
            console_set_colors(0xff101624,0xffe6eaf4);
            console_clrscr();
            printf("\n  XENONFIN - Native PowerPC boot diagnostic\n\n");
            printf("  ELF for XeLL. Not an XEX or a finished console client.\n\n");
            for(int i=0;i<4;++i) printf("  %c %s\n",i==selected?'>':' ',sections[i]);
            printf("\n  D-pad / LB / RB: move     A: test     B: return to XeLL\n\n");
            if(selected==0) printf("  Graphics initialized; controller input is polled.\n  Verify display, all buttons, unplug/replug on hardware.\n");
            if(selected==1) {
                printf("  Press A to initialize Ethernet / DHCP. Status: %d\n",network_status);
                if(network_status==NETWORK_INIT_SUCCESS) network_print_config();
            }
            if(selected==2) printf("  Press A for a short 440 Hz stereo PCM tone.\n");
            if(selected==3) printf("  Remaining: Xenon TLS / HTTP port, asynchronous UI,\n  FFmpeg port, video textures, audio clock, hardware tests.\n  No public hardware video-decoder API has been verified.\n");
            redraw=0;
        }
        if(network_status==NETWORK_INIT_SUCCESS) network_poll();
        previous=pad;
        udelay(10000);
    }
}
