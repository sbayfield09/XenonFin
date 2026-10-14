/* Shared by the actual XeLL build and host protocol tests. Fixed storage only. */
#include "browse.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

static int guid(const char *s) {
    size_t n = strlen(s), i;
    if (n != 32 && n != 36) return 0;
    for (i = 0; i < n; ++i) {
        char c = s[i];
        if (n == 36 && (i == 8 || i == 13 || i == 18 || i == 23)) { if (c != '-') return 0; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}
static int is_null(xf_json_ref r) { return r.size == 4 && !memcmp(r.data, "null", 4); }
static int text(xf_json_ref object, const char *path, char *out, size_t cap) {
    xf_json_ref r; int rc = xf_json_get(object, path, &r);
    out[0] = 0;
    return rc == 0 || (rc == 1 && (is_null(r) || xf_json_text(r, out, cap)));
}
static int number(xf_json_ref object, const char *path, uint64_t *out) {
    xf_json_ref r; int rc = xf_json_get(object, path, &r);
    *out = 0;
    return rc == 0 || (rc == 1 && (is_null(r) || xf_json_uint(r, out)));
}
static int boolean(xf_json_ref object, const char *path, int *out) {
    xf_json_ref r; int rc = xf_json_get(object, path, &r);
    *out = 0;
    return rc == 0 || (rc == 1 && (is_null(r) || xf_json_bool(r, out)));
}
static int item(xf_json_ref object, xf_item *out) {
    xf_json_ref r, strings[3]; size_t count, i, used = 0; uint64_t n; int rc;
    memset(out, 0, sizeof(*out));
    if (xf_json_string(object.data, object.size, "Id", out->id, sizeof(out->id)) != 1 || !guid(out->id) ||
        !text(object, "Name", out->name, sizeof(out->name)) || !out->name[0] ||
        !text(object, "Type", out->type, sizeof(out->type)) || !out->type[0] ||
        !text(object, "CollectionType", out->collection, sizeof(out->collection)) ||
        !text(object, "MediaType", out->media, sizeof(out->media)) ||
        !text(object, "Overview", out->overview, sizeof(out->overview)) ||
        !text(object, "SeriesName", out->series, sizeof(out->series)) ||
        !text(object, "Album", out->album, sizeof(out->album)) ||
        !text(object, "OfficialRating", out->rating, sizeof(out->rating)) ||
        !number(object, "RunTimeTicks", &out->ticks) || !number(object, "UserData.PlaybackPositionTicks", &out->position) ||
        !boolean(object, "IsFolder", &out->folder) || !boolean(object, "UserData.Played", &out->played)) return 0;
#define UINT_FIELD(key, field) do { if (!number(object, key, &n) || n > UINT_MAX) return 0; out->field = (unsigned)n; } while (0)
    UINT_FIELD("ProductionYear", year); UINT_FIELD("IndexNumber", index);
    UINT_FIELD("ParentIndexNumber", season); UINT_FIELD("ChildCount", children);
#undef UINT_FIELD
    rc = xf_json_get(object, "Artists", &r); if (rc < 0) return 0;
    if (rc == 1 && !is_null(r)) {
        if (!xf_json_elements(r, 0, strings, 1, &count) || (count && !xf_json_text(strings[0], out->artist, sizeof(out->artist)))) return 0;
    }
    rc = xf_json_get(object, "Genres", &r); if (rc < 0) return 0;
    if (rc == 1 && !is_null(r)) {
        if (!xf_json_elements(r, 0, strings, 3, &count)) return 0;
        for (i = 0; i < count && i < 3; ++i) {
            char genre[49]; int added;
            if (!xf_json_text(strings[i], genre, sizeof(genre))) return 0;
            added = snprintf(out->genres + used, sizeof(out->genres) - used, "%s%s", i ? ", " : "", genre);
            if (added < 0 || (size_t)added >= sizeof(out->genres) - used) return 0;
            used += (size_t)added;
        }
    }
    return 1;
}
int xf_parse_page(const char *json, size_t length, unsigned window, int views, xf_page *out) {
    xf_json_ref root = { json, length }, array, entries[XF_PAGE_SIZE], r;
    size_t count, available, i; uint64_t total; int rc;
    memset(out, 0, sizeof(*out));
    if (xf_json_get(root, "Items", &array) != 1 ||
        !xf_json_elements(array, views ? window : 0, entries, XF_PAGE_SIZE, &count)) return 0;
    if (!views && count > XF_PAGE_SIZE) return 0;
    if (views) {
        if (count > UINT_MAX) return 0;
        total = count; available = window < count ? count - window : 0;
    } else {
        if (xf_json_get(root, "TotalRecordCount", &r) != 1 || !xf_json_uint(r, &total) ||
            total > UINT_MAX || count > total || (count && window > total - count)) return 0;
        rc = xf_json_get(root, "StartIndex", &r); if (rc < 0) return 0;
        if (rc == 1) {
            uint64_t start;
            if (!xf_json_uint(r, &start) || start != window) return 0;
        }
        available = count;
    }
    out->count = (unsigned)(available > XF_PAGE_SIZE ? XF_PAGE_SIZE : available); out->total = (unsigned)total;
    for (i = 0; i < out->count; ++i) if (!item(entries[i], &out->items[i])) return 0;
    return 1;
}
void xf_browser_init(xf_browser *b) { xf_wipe(b, sizeof(*b)); snprintf(b->current.title, sizeof(b->current.title), "Libraries"); }
void xf_browser_move(xf_browser *b, int delta) {
    if (!b->busy && b->details) {
        if (delta < 0 && b->detail_scroll) --b->detail_scroll;
        if (delta > 0 && (b->detail_scroll + 4) * 68 < strlen(b->detail.overview)) ++b->detail_scroll;
        return;
    }
    if (!b->busy && !b->details && b->page.count)
        b->current.selected = (b->current.selected + b->page.count + delta) % b->page.count;
}
enum { REPLACE, PUSH, POP, RESET, DETAIL };
int xf_browser_request(xf_browser *b, enum xf_browse_action action, const char *user, char *path, size_t cap) {
    const xf_item *selected = b->loaded && b->page.count ? &b->page.items[b->current.selected] : NULL;
    int n;
    if (b->busy) return 0;
    if (action == XF_BACK && b->details) { b->details = 0; return 0; }
    if (!guid(user)) { snprintf(b->message, sizeof(b->message), "Sign in with a valid Jellyfin user first."); return -1; }
    b->pending = b->current; b->operation = REPLACE;
    if (action == XF_ROOT || (!b->loaded && action == XF_REFRESH)) {
        memset(&b->pending, 0, sizeof(b->pending)); strcpy(b->pending.title, "Libraries"); b->operation = RESET;
    } else if (action == XF_BACK) {
        if (!b->depth) return 0;
        b->pending = b->history[b->depth - 1]; b->operation = POP;
    } else if (action == XF_NEXT) {
        if (b->details || !b->page.count || b->page.total - b->current.start <= b->page.count) return 0;
        b->pending.start += b->page.count; b->pending.selected = 0;
    } else if (action == XF_PREVIOUS) {
        if (b->details || !b->current.start) return 0;
        b->pending.start = b->current.start > XF_PAGE_SIZE ? b->current.start - XF_PAGE_SIZE : 0; b->pending.selected = 0;
    } else if (action == XF_MUSIC) {
        if (b->details || b->current.view < XF_ARTISTS || b->current.view > XF_TRACKS) return 0;
        b->pending.view = b->current.view == XF_TRACKS ? XF_ARTISTS : b->current.view + 1;
        b->pending.start = b->pending.selected = 0;
    } else if (action == XF_OPEN || action == XF_DETAILS) {
        if (!selected || b->details) return 0;
        if (action == XF_DETAILS || !(selected->folder || !strcmp(selected->type, "Series") ||
            !strcmp(selected->type, "Season") || !strcmp(selected->type, "MusicAlbum") || !strcmp(selected->type, "MusicArtist"))) {
            b->operation = DETAIL; strcpy(b->pending.id, selected->id);
        } else {
            if (b->depth == XF_HISTORY_LIMIT) { snprintf(b->message, sizeof(b->message), "Navigation depth limit (12). Go back first."); return -1; }
            b->operation = PUSH; b->pending.view = XF_CHILDREN;
            strcpy(b->pending.id, selected->id); b->pending.start = b->pending.selected = 0;
            /* snprintf bounds the breadcrumb without allocating a copy of the title. */
            memcpy(b->pending.title, selected->name, sizeof(b->pending.title) - 1); b->pending.title[sizeof(b->pending.title) - 1] = 0;
            if (!strcmp(selected->collection, "music")) {
                b->pending.view = XF_ARTISTS; strcpy(b->pending.scope, selected->id);
            } else if (!strcmp(selected->type, "MusicArtist")) b->pending.view = XF_ARTIST_ALBUMS;
        }
    }
    if (b->operation == DETAIL)
        n = snprintf(path, cap, "/Users/%s/Items?Ids=%s&Limit=1&StartIndex=0&Fields=Overview,Genres&EnableImages=false&EnableUserData=true", user, b->pending.id);
    else if (b->pending.view == XF_VIEWS)
        n = snprintf(path, cap, "/Users/%s/Views?IncludeExternalContent=false", user);
    else {
        char filter[300];
        const char *sort = b->pending.view == XF_CHILDREN ? "IndexNumber,SortName" : "SortName";
        if (b->pending.view == XF_ARTIST_ALBUMS)
            snprintf(filter, sizeof(filter), "&ParentId=%s&ArtistIds=%s&Recursive=true&IncludeItemTypes=MusicAlbum", b->pending.scope, b->pending.id);
        else if (b->pending.view == XF_ARTISTS || b->pending.view == XF_ALBUMS || b->pending.view == XF_TRACKS)
            snprintf(filter, sizeof(filter), "&ParentId=%s&Recursive=true%s", b->pending.scope,
                     b->pending.view == XF_ALBUMS ? "&IncludeItemTypes=MusicAlbum" : b->pending.view == XF_TRACKS ? "&IncludeItemTypes=Audio" : "");
        else snprintf(filter, sizeof(filter), "&ParentId=%s", b->pending.id);
        n = snprintf(path, cap, "%s?UserId=%s&StartIndex=%u&Limit=%u&SortBy=%s&SortOrder=Ascending&Fields=ChildCount&EnableImages=false&EnableUserData=true&EnableTotalRecordCount=true%s",
            b->pending.view == XF_ARTISTS ? "/Artists" : "/Items", user, b->pending.start, XF_PAGE_SIZE, sort, filter);
    }
    if (n < 0 || (size_t)n >= cap) { snprintf(b->message, sizeof(b->message), "Library request exceeds path limit."); return -1; }
    b->busy = 1; snprintf(b->message, sizeof(b->message), "Loading Jellyfin library data; B cancels."); return 1;
}
int xf_browser_result(xf_browser *b, const xf_response *r, const char *error) {
    if (!b->busy) return 0;
    b->busy = 0; b->status = r->status;
    if (r->status == 401) { snprintf(b->message, sizeof(b->message), "HTTP 401: session expired. Sign in again."); return -2; }
    if (error && *error) { snprintf(b->message, sizeof(b->message), "HTTP %d: %.125s. X retries current page.", r->status, error); return -1; }
    if (!r->complete || r->failed || r->status != 200) {
        snprintf(b->message, sizeof(b->message), r->status == 403 ? "HTTP 403: library access denied." : r->status == 404 ? "HTTP 404: item removed or endpoint unavailable." : "HTTP %d: library request failed.", r->status); return -1;
    }
    if (!xf_parse_page(r->body, r->body_size, b->operation == DETAIL ? 0 : b->pending.start,
                       b->operation != DETAIL && b->pending.view == XF_VIEWS, &b->staging) ||
        (b->operation == DETAIL && (b->staging.count != 1 || strcmp(b->staging.items[0].id, b->pending.id)))) {
        xf_wipe(&b->staging, sizeof(b->staging));
        snprintf(b->message, sizeof(b->message), "HTTP 200: invalid/inconsistent library JSON. Previous screen retained."); return -1;
    }
    if (b->operation == DETAIL) { b->detail = b->staging.items[0]; b->details = 1; b->detail_scroll = 0; }
    else {
        if (b->operation == PUSH) b->history[b->depth++] = b->current;
        if (b->operation == POP) --b->depth;
        if (b->operation == RESET) b->depth = 0;
        b->current = b->pending; b->page = b->staging; b->loaded = 1; b->details = 0;
        if (b->current.selected >= b->page.count) b->current.selected = 0;
    }
    xf_wipe(&b->staging, sizeof(b->staging));
    snprintf(b->message, sizeof(b->message), b->details ? "HTTP 200: item details. B returns to list." : b->page.count ? "HTTP 200: library page loaded." : "HTTP 200: no items on this page. B goes back; X refreshes.");
    return 1;
}
