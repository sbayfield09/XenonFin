#ifndef XF_MUSIC_API_H
#define XF_MUSIC_API_H
#include "protocol.h"
typedef struct { char source_id[65], session_id[65], path[1501]; int transcode; } xf_music_source;
int xf_music_id(const char *);
int xf_music_profile(char *, size_t, const char *user);
int xf_music_source_parse(const xf_response *, const char *item_id, xf_music_source *);
int xf_music_report(char *, size_t, const char *item_id, const xf_music_source *, uint64_t ticks, int paused);
#endif
