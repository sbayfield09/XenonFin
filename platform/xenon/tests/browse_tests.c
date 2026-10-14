#include "../browse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "browse line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static const char user[] = "11111111111111111111111111111111";
static const char library[] = "{\"Items\":[{\"Id\":\"22222222222222222222222222222222\",\"Name\":\"Test Movies\",\"Type\":\"CollectionFolder\",\"CollectionType\":\"movies\",\"IsFolder\":true}],\"TotalRecordCount\":1,\"StartIndex\":0}";
static xf_browser b;
static xf_response response;
static xf_page page;
static char path[1501], request[XF_REQUEST_LIMIT], document[XF_BODY_LIMIT+1];
static int result(const char *json, int status, const char *error) {
    xf_response_init(&response); response.complete = 1; response.status = status;
    response.body_size = strlen(json); memcpy(response.body, json, response.body_size + 1);
    return xf_browser_result(&b, &response, error);
}
static int action(enum xf_browse_action a) { return xf_browser_request(&b, a, user, path, sizeof(path)); }
int main(void) {
    xf_endpoint ep; char error[129], text[8]; xf_json_ref r; uint64_t n; size_t count; unsigned i;
    CHECK(xf_endpoint_parse("192.168.68.202", &ep, error, sizeof(error)));
    CHECK(xf_get_request(request, sizeof(request), &ep, "device", "session-secret", "/Users/test/Views") > 0);
    CHECK(strstr(request, "GET /Users/test/Views HTTP/1.1\r\n")); CHECK(strstr(request, "Token=\"session-secret\""));
    CHECK(xf_get_request(request, sizeof(request), &ep, "device", "bad\r\nInjected", "/Items") < 0 && !request[0]);
    CHECK(xf_get_request(request, sizeof(request), &ep, "device", "bad\"token", "/Items") < 0);
    CHECK(xf_get_request(request, sizeof(request), &ep, "device", "token", "//evil/Items") < 0);
    CHECK(xf_get_request(request, sizeof(request), &ep, "device", "token", "/Items\r\nInjected") < 0);
    CHECK(xf_get_request(request, 12, &ep, "device", "token", "/Items") < 0 && !request[0]);
    r.data = "18446744073709551615"; r.size = strlen(r.data); CHECK(xf_json_uint(r, &n) && n == UINT64_MAX);
    r.data = "18446744073709551616"; CHECK(!xf_json_uint(r, &n));
    r.data = "\"a\\ud83d\\ude00z\""; r.size = strlen(r.data);
    CHECK(xf_json_text(r, text, 5) && !strcmp(text, "a"));
    CHECK(xf_json_text(r, text, 6) && strlen(text) == 5);
    r.data = "\"a\xf0\x9f\x98\x80z\""; r.size = strlen(r.data);
    CHECK(xf_json_text(r, text, 4) && !strcmp(text, "a"));
    r.data = "[{},1,false]"; r.size = strlen(r.data); CHECK(xf_json_elements(r, 10, NULL, 0, &count) && count == 3);
    r.data = "[{},]"; r.size = strlen(r.data); CHECK(!xf_json_elements(r, 0, NULL, 0, &count));
    CHECK(xf_parse_page(library, strlen(library), 0, 1, &page) && page.count == 1 && page.total == 1);
    CHECK(!xf_parse_page("{\"Items\":[],\"Items\":[]}", 23, 0, 1, &page));
    xf_browser_init(&b);
    CHECK(action(XF_ROOT) == 1 && strstr(path, "/Views?")); CHECK(action(XF_ROOT) == 0);
    CHECK(result(library, 200, "") == 1 && b.loaded && !b.depth && b.page.count == 1);
    CHECK(action(XF_OPEN) == 1 && strstr(path, "Limit=10") && strstr(path, "ParentId=2222"));
    CHECK(result("{}", 200, "") == -1 && !b.depth && b.page.count == 1 && b.current.view == XF_VIEWS);
    CHECK(action(XF_OPEN) == 1); CHECK(result("", 0, "Request cancelled") == -1 && !b.busy && !b.depth);
    CHECK(action(XF_OPEN) == 1); CHECK(result("", 403, "") == -1 && strstr(b.message, "403") && b.loaded);
    CHECK(action(XF_OPEN) == 1); CHECK(result("{\"Items\":[],\"TotalRecordCount\":0,\"StartIndex\":0}", 200, "") == 1 && b.depth == 1 && !b.page.count);
    CHECK(action(XF_NEXT) == 0); CHECK(action(XF_BACK) == 1); CHECK(result(library, 200, "") == 1 && !b.depth);
    /* Root views cannot be server-paginated: extract a window without storing all libraries. */
    strcpy(document, "{\"Items\":[");
    for (i = 0; i < 12; ++i) {
        char entry[240]; snprintf(entry, sizeof(entry), "%s{\"Id\":\"%032u\",\"Name\":\"Library %u\",\"Type\":\"CollectionFolder\",\"IsFolder\":true}", i ? "," : "", i + 1, i);
        strcat(document, entry);
    }
    strcat(document, "],\"TotalRecordCount\":12}");
    CHECK(action(XF_ROOT) == 1 && result(document, 200, "") == 1 && b.page.count == 10);
    CHECK(action(XF_NEXT) == 1 && result(document, 200, "") == 1 && b.page.count == 2 && b.current.start == 10);
    CHECK(action(XF_NEXT) == 0); CHECK(action(XF_PREVIOUS) == 1 && result(document, 200, "") == 1 && b.current.start == 0);
    CHECK(!xf_parse_page(document, strlen(document), 0, 0, &page)); /* Reject over-limit item responses. */
    CHECK(action(XF_REFRESH) == 1 && result("", 401, "") == -2 && !b.busy);
    xf_browser_init(&b); CHECK(!b.loaded && !b.page.count && !b.detail.id[0]);
    CHECK(xf_browser_request(&b, XF_ROOT, "a/../../bad", path, sizeof(path)) == -1);
    CHECK(action(XF_ROOT) == 1 && result(library, 200, "") == 1);
    /* Movie details and UTF-8/long overview truncation. */
    strcpy(b.page.items[0].type, "Movie"); b.page.items[0].folder = 0;
    CHECK(action(XF_OPEN) == 1 && strstr(path, "Ids=2222") && strstr(path, "Fields=Overview,Genres"));
    strcpy(document, "{\"Items\":[{\"Id\":\"22222222222222222222222222222222\",\"Name\":\"Movie \\u00e9\",\"Type\":\"Movie\",\"ProductionYear\":2024,\"RunTimeTicks\":900000000,\"UserData\":{\"Played\":true},\"Genres\":[\"Drama\"],\"Overview\":\"");
    for (i = 0; i < 1600; ++i) strcat(document, "x");
    strcat(document, "\"}],\"TotalRecordCount\":1,\"StartIndex\":0}");
    CHECK(result(document, 200, "") == 1 && b.details && strlen(b.detail.overview) == 768 && b.detail.played && b.detail.year == 2024 && !strcmp(b.detail.genres, "Drama"));
    xf_browser_move(&b, -1); CHECK(!b.detail_scroll);
    for (i = 0; i < 30; ++i) xf_browser_move(&b, 1);
    CHECK(b.detail_scroll == 8); xf_browser_move(&b, -1); CHECK(b.detail_scroll == 7);
    CHECK(action(XF_BACK) == 0 && !b.details && b.page.count == 1);
    /* Music library -> artist -> album -> track and alternate music views. */
    strcpy(b.page.items[0].collection, "music"); b.page.items[0].folder = 1;
    CHECK(action(XF_OPEN) == 1 && !strncmp(path, "/Artists?", 9));
    CHECK(result(library, 200, "") == 1 && b.current.view == XF_ARTISTS);
    CHECK(action(XF_MUSIC) == 1 && strstr(path, "IncludeItemTypes=MusicAlbum"));
    CHECK(result(library, 200, "") == 1 && b.current.view == XF_ALBUMS);
    CHECK(action(XF_MUSIC) == 1 && strstr(path, "IncludeItemTypes=Audio"));
    CHECK(result(library, 200, "") == 1 && b.current.view == XF_TRACKS);
    CHECK(action(XF_MUSIC) == 1 && result(library, 200, "") == 1 && b.current.view == XF_ARTISTS);
    strcpy(b.page.items[0].type, "MusicArtist"); b.page.items[0].collection[0] = 0;
    CHECK(action(XF_OPEN) == 1 && strstr(path, "ArtistIds=2222"));
    CHECK(result(library, 200, "") == 1 && b.current.view == XF_ARTIST_ALBUMS);
    strcpy(b.page.items[0].type, "MusicAlbum"); b.page.items[0].collection[0] = 0;
    CHECK(action(XF_OPEN) == 1 && strstr(path, "ParentId=2222") && !strstr(path, "ArtistIds"));
    CHECK(result(library, 200, "") == 1 && b.current.view == XF_CHILDREN);
    strcpy(b.page.items[0].type, "Audio"); b.page.items[0].folder = 0;
    CHECK(action(XF_OPEN) == 1 && result(document, 200, "") == 1 && b.details);
    CHECK(action(XF_BACK) == 0); b.depth = XF_HISTORY_LIMIT; b.page.items[0].folder = 1;
    CHECK(action(XF_OPEN) == -1 && !b.busy);
    /* Wrong pagination offsets must not replace the current screen. */
    CHECK(!xf_parse_page("{\"Items\":[],\"TotalRecordCount\":50,\"StartIndex\":1}", strlen("{\"Items\":[],\"TotalRecordCount\":50,\"StartIndex\":1}"), 10, 0, &page));
    printf("Native browsing/metadata/pagination/session/error handling: %u checks passed\n", checks);
    return 0;
}
