#ifndef XF_VIDEO_NATIVE_H
#define XF_VIDEO_NATIVE_H
#include "video.h"
void xf_video_native_backend(xf_video_backend *, int gpu);
int xf_video_local_path(char *path, size_t capacity, char *error, size_t error_capacity);
#endif
