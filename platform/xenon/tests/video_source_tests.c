#define _POSIX_C_SOURCE 200809L
#include "../video_native.h"
#include <libfat/fat.h>
#include <diskio/disc_io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static unsigned checks,mounts;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"video source line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
DISC_INTERFACE usb2mass_ops_0,usb2mass_ops_1,usb2mass_ops_2;
bool fatMountSimple(const char *name,const DISC_INTERFACE *disc) {
    ++mounts;
    if(!strcmp(name,"uda")) {CHECK(disc==&usb2mass_ops_0);return false;}
    if(!strcmp(name,"udb")) {CHECK(disc==&usb2mass_ops_1);return true;}
    CHECK(!strcmp(name,"udc") && disc==&usb2mass_ops_2);return false;
}
int main(void) {
    char directory[]="/tmp/xenonfin-video-source-XXXXXX",path[128],error[180];FILE *file;unsigned before;
    CHECK(mkdtemp(directory)!=NULL && !chdir(directory));
    CHECK(!xf_video_local_path(path,sizeof(path),error,sizeof(error)) && strstr(error,"No FAT USB"));CHECK(mounts==3);
    CHECK(!mkdir("udb:",0700));file=fopen("udb:/xenonfin-test.m1v","wb");CHECK(file!=NULL);fputc(1,file);fclose(file);
    before=mounts;CHECK(xf_video_local_path(path,sizeof(path),error,sizeof(error)));
    CHECK(!strcmp(path,"udb:/xenonfin-test.m1v") && mounts==before+1); /* Retain existing udb mount. */
    CHECK(!unlink(path) && !rmdir("udb:"));CHECK(!chdir("/tmp") && !rmdir(directory));
    printf("Native FAT USB interface selection, mount retention and local path errors: %u assertions passed\n",checks);return 0;
}
