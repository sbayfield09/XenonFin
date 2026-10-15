#include "video_native.h"
#include <libfat/fat.h>
#include <diskio/disc_io.h>
#include <stdio.h>
extern DISC_INTERFACE usb2mass_ops_0, usb2mass_ops_1, usb2mass_ops_2;
int xf_video_local_path(char *path, size_t capacity, char *error, size_t error_capacity) {
    static unsigned mounted;
    DISC_INTERFACE *interfaces[]={&usb2mass_ops_0,&usb2mass_ops_1,&usb2mass_ops_2};
    const char *names[]={"uda","udb","udc"}; unsigned i;
    for(i=0;i<3;++i) {
        FILE *file;
        /* Only the USB interfaces already serviced by usb_do_poll(). Do not
         * initialize HDD/DVD or assume FATX. Mounted volumes are retained. */
        if(!(mounted&(1U<<i))) {
            if(!fatMountSimple(names[i],interfaces[i])) continue;
            mounted|=1U<<i;
        }
        snprintf(path,capacity,"%s:/xenonfin-test.m1v",names[i]);
        file=fopen(path,"rb"); if(file) { fclose(file); return 1; }
    }
    snprintf(error,error_capacity,"No FAT USB root xenonfin-test.m1v found on uda:/, udb:/ or udc:/. Keep the drive inserted."); return 0;
}
