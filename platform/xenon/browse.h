#ifndef XF_BROWSE_H
#define XF_BROWSE_H
#include "protocol.h"
#define XF_PAGE_SIZE 10
#define XF_HISTORY_LIMIT 12
typedef struct {
    char id[65], name[193], type[33], collection[33], media[33];
    char overview[769], series[129], album[129], artist[129], rating[33], genres[161];
    uint64_t ticks, position;
    unsigned year, index, season, children;
    int folder, played;
} xf_item;
typedef struct { xf_item items[XF_PAGE_SIZE]; unsigned count, total; } xf_page;
enum xf_view { XF_VIEWS, XF_CHILDREN, XF_ARTISTS, XF_ALBUMS, XF_TRACKS, XF_ARTIST_ALBUMS };
typedef struct {
    enum xf_view view;
    char id[65], scope[65], title[97];
    unsigned start, selected;
} xf_location;
enum xf_browse_action { XF_ROOT, XF_REFRESH, XF_OPEN, XF_DETAILS, XF_BACK, XF_NEXT, XF_PREVIOUS, XF_MUSIC };
typedef struct {
    xf_location current, pending, history[XF_HISTORY_LIMIT];
    xf_page page, staging;
    xf_item detail;
    unsigned depth, detail_scroll;
    int loaded, details, busy, operation, status;
    char message[180];
} xf_browser;
void xf_browser_init(xf_browser *);
/* 1: path ready, 0: local action/no request, -1: cannot navigate. */
int xf_browser_request(xf_browser *, enum xf_browse_action, const char *user_id, char *path, size_t);
/* -2 means expired session; caller must erase token/user/browser. */
int xf_browser_result(xf_browser *, const xf_response *, const char *transport_error);
int xf_parse_page(const char *, size_t, unsigned window, int views, xf_page *);
void xf_browser_move(xf_browser *, int delta);
#endif
