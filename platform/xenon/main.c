/* Extend the hardware-tested XeLL diagnostic; graphics, USB/controller, sound,
 * Ethernet driver and the original four diagnostic pages remain in place. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <xenos/xenos.h>
#include <xenon_sound/sound.h>
#include <network/network.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <pci/io.h>
#include <xenon_soc/xenon_power.h>
#include "native_net.h"
#include "driver_guard.h"
#include "browse.h"
#include "music.h"
#include "sound_diag.h"
#include "text.h"
#include "console_batch.h"
#include "ui_clock.h"
#include "video_native.h"
#include "jvideo.h"
#include "framebuffer.h"
#include "timing.h"
/* Application UI output goes directly to the composed text frame. */
#define printf xf_console_printf

static const char *sections[] = {
    "Graphics / controller", "Network / DHCP diagnostics", "Audio output",
    "Platform status", "Jellyfin server", "Jellyfin login", "Libraries", "Now playing", "Video / local test"
};
static xf_http_client http;
static xf_browser browser;
enum { SECTION_COUNT = sizeof(sections)/sizeof(*sections) };
static xf_music music;
static xf_sound_diag sound_test;
static unsigned audio_resets;
static xf_video video;
static xf_jvideo jvideo;
static xf_item selected_video;
static unsigned av_stamp_page;
static unsigned video_metrics_page;
static unsigned text_width = 72;
enum { REQUEST_PUBLIC, REQUEST_LOGIN, REQUEST_BROWSE };
static int request_kind;
static xf_keyboard keyboard;
static xf_endpoint endpoint;
static char address[XF_TEXT_LIMIT + 1], username[XF_TEXT_LIMIT + 1], password[XF_TEXT_LIMIT + 1];
static char server_name[129], version[65], token[2049], user_id[129], user_name[129];
static char result[180] = "Edit server IP/port, initialise Network, then test connection.";
static int server_valid, server_verified, authenticated, processed = 1;
static int editing, edit_field, login_selected, show_body;
static void handle_result(void);
/* Each time check uses elapsed subtraction, including uint32 millisecond wrap. */
uint64_t xf_monotonic_us(void) {
    uint32_t high, low, check;
    do {
        asm volatile ("mftbu %0" : "=r" (high));
        asm volatile ("mftbl %0" : "=r" (low));
        asm volatile ("mftbu %0" : "=r" (check));
    } while (high != check);
    return xf_ticks_to_us(((uint64_t)high << 32) | low,PPC_TIMEBASE_FREQ);
}
uint32_t xf_monotonic_ms(void) { return (uint32_t)(xf_monotonic_us()/1000); }
static void service_network(uint32_t now) {
    uint64_t start=xf_monotonic_us();xf_network_tick(now);
    if(xf_jvideo_active(&jvideo)) {
        uint64_t cost=xf_monotonic_us()-start;
        jvideo.network_us+=cost;xf_time_add(&jvideo.network_times,cost);
    }
}
static void logout(void) {
    xf_music_forget(&music);xf_jvideo_forget(&jvideo);
    authenticated = 0; xf_wipe(token, sizeof(token)); xf_wipe(user_id, sizeof(user_id)); xf_wipe(user_name, sizeof(user_name));
    xf_browser_init(&browser);
}
static void safe_print(const char *text, size_t max) {
    size_t i;
    for (i = 0; i < max && text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        /* Never interpret server-controlled terminal escapes/control bytes. */
        xf_console_putch(c >= 32 && c < 127 ? c : '.');
    }
}
static void begin_edit(int field) {
    xf_http_cancel(&http); handle_result(); processed = 1; editing = 1; edit_field = field;
    xf_keyboard_init(&keyboard, field == 1 ? address : field == 2 ? username : "", field == 3);
}
static void finish_edit(void) {
    if (edit_field == 1) {
        char error[128]; xf_endpoint candidate;
        if (!xf_endpoint_parse(keyboard.value, &candidate, error, sizeof(error))) {
            snprintf(result, sizeof(result), "%s", error); return;
        }
        if (strcmp(address, keyboard.value)) { server_verified = 0; logout(); server_name[0] = version[0] = 0; }
        endpoint = candidate; server_valid = 1;
        snprintf(address, sizeof(address), "%s:%u", endpoint.host, endpoint.port);
        snprintf(result, sizeof(result), "Server set. A test /System/Info/Public; HTTP is plaintext LAN only.");
    } else if (edit_field == 2) { if (strcmp(username, keyboard.value)) logout(); snprintf(username, sizeof(username), "%s", keyboard.value); }
    else { logout(); xf_wipe(password, sizeof(password)); snprintf(password, sizeof(password), "%s", keyboard.value); }
    xf_wipe(&keyboard, sizeof(keyboard)); editing = 0;
}
static void start_request(int login, uint32_t now) {
    char device[65];
    if (!server_valid) { snprintf(result, sizeof(result), "Set a numeric server IP and port first (Y on server page)."); return; }
    if (!xf_network_ready()) { snprintf(result, sizeof(result), "No verified DHCP lease. A on Network to initialise/retry."); return; }
    if (login && !server_verified) { snprintf(result, sizeof(result), "Test /System/Info/Public successfully before login."); return; }
    if (login && !username[0]) { snprintf(result, sizeof(result), "Enter username first."); return; }
    snprintf(device, sizeof(device), "xenonfin-%02x%02x%02x%02x%02x%02x", netif.hwaddr[0], netif.hwaddr[1], netif.hwaddr[2], netif.hwaddr[3], netif.hwaddr[4], netif.hwaddr[5]);
    if (login) logout(); else server_verified = 0;
    xf_http_begin(&http, &endpoint, device, login ? username : NULL, login ? password : NULL, now);
    request_kind = login ? REQUEST_LOGIN : REQUEST_PUBLIC;
    if (login) xf_wipe(password, sizeof(password)); /* The copied request is wiped after ACK/cleanup. */
    processed = 0; show_body = 0;
    snprintf(result, sizeof(result), "%s; B cancels. Connect/idle 8s, total 20s.", login ? "Signing in" : "GET /System/Info/Public");
}
static void browse_request(enum xf_browse_action action, uint32_t now) {
    char device[65], path[1501]; int rc;
    if (http.active) return;
    if (!authenticated) { snprintf(result, sizeof(result), "Sign in on the Jellyfin login page before browsing."); return; }
    if (!xf_network_ready()) { snprintf(result, sizeof(result), "DHCP lease unavailable. Retry on Network; then X refresh."); return; }
    rc = xf_browser_request(&browser, action, user_id, path, sizeof(path));
    if (rc == 1) {
        snprintf(device, sizeof(device), "xenonfin-%02x%02x%02x%02x%02x%02x", netif.hwaddr[0], netif.hwaddr[1], netif.hwaddr[2], netif.hwaddr[3], netif.hwaddr[4], netif.hwaddr[5]);
        xf_http_get(&http, &endpoint, device, token, path, now);
        request_kind = REQUEST_BROWSE; processed = 0;
    }
    snprintf(result, sizeof(result), "%s", browser.message);
}
static void handle_result(void) {
    if (processed || !http.done || http.active) return;
    processed = 1;
    if (request_kind == REQUEST_BROWSE) {
        int rc = xf_browser_result(&browser, &http.response, http.error);
        snprintf(result, sizeof(result), "%s", browser.message);
        if (rc == -2) logout();
        xf_wipe(&http.response, sizeof(http.response)); return;
    }
    if (http.error[0]) {
        if (http.response.status) snprintf(result, sizeof(result), "HTTP %d: %s (lwIP %d)", http.response.status, http.error, http.error_code);
        else snprintf(result, sizeof(result), "%s (lwIP %d; no HTTP status)", http.error, http.error_code);
    }
    else if (request_kind == REQUEST_LOGIN) {
        if (http.response.status == 401) snprintf(result, sizeof(result), "HTTP 401: incorrect username/password. Enter password again.");
        else if (http.response.status == 403) snprintf(result, sizeof(result), "HTTP 403: account or server access denied.");
        else if (xf_auth_info(&http.response, token, sizeof(token), user_id, sizeof(user_id), user_name, sizeof(user_name))) {
            authenticated = 1; snprintf(result, sizeof(result), "HTTP 200: authenticated. Token held in RAM, never printed/saved.");
        } else snprintf(result, sizeof(result), "HTTP %d: invalid authentication response (body hidden).", http.response.status);
    } else if (xf_public_info(&http.response, server_name, sizeof(server_name), version, sizeof(version))) {
        server_verified = 1; snprintf(result, sizeof(result), "HTTP 200: Jellyfin connection verified. Login is now available.");
    } else if (http.response.status >= 300 && http.response.status < 400)
        snprintf(result, sizeof(result), "HTTP %d: redirects unsupported. Use direct LAN IPv4:8096.", http.response.status);
    else snprintf(result, sizeof(result), "HTTP %d: response was not valid Jellyfin server information.", http.response.status);
    if (http.sensitive) xf_wipe(&http.response, sizeof(http.response)); /* Never render auth JSON, even on failure. */
}
static void wrapped(const char *text, unsigned skip, unsigned lines) {
    char line[161]; size_t at = 0; unsigned n = 0, printed = 0;
    while (xf_text_line(text, &at, text_width, line, sizeof(line))) {
        if (n++ < skip) continue;
        if (printed++ == lines) break;
        printf("  %s\n", line);
    }
}
static void cell(const char *text, unsigned width) {
    char out[161]; xf_text_cell(out, sizeof(out), text, width); printf("%s", out);
}
static int start_network_video(const xf_item *it,uint32_t now) {
    char device[65];xf_video_backend backend;
    if(!authenticated || !xf_network_ready()) {snprintf(result,sizeof(result),"Sign in and verify networking before video playback.");return 0;}
    if(xf_music_active(&music) || xf_sound_diag_active(&sound_test) || xenon_sound_get_unplayed() || http.active || xf_jvideo_active(&jvideo)) {
        snprintf(result,sizeof(result),"Stop music/video and wait for current requests before starting video.");return 0;
    }
    snprintf(device,sizeof(device),"xenonfin-%02x%02x%02x%02x%02x%02x",netif.hwaddr[0],netif.hwaddr[1],netif.hwaddr[2],netif.hwaddr[3],netif.hwaddr[4],netif.hwaddr[5]);
    xf_video_native_backend(&backend,1);
    if(!xf_jvideo_play(&jvideo,&endpoint,device,token,user_id,it,&backend,now))return 0;
    selected_video=*it;return 2;
}
static int play_selected(uint32_t now) {
    const xf_item *it=browser.details?&browser.detail:browser.page.count?&browser.page.items[browser.current.selected]:NULL;
    char device[65];
    if(!it)return 0;
    if(browser.details && (!strcmp(it->type,"Movie") || !strcmp(it->type,"Episode"))){int rc=start_network_video(it,now);return rc?rc:-1;}
    if(strcmp(it->type,"Audio"))return 0;
    if(!authenticated || !xf_network_ready()) {snprintf(result,sizeof(result),"Sign in and verify networking before playing music.");return 0;}
    if(xf_jvideo_active(&jvideo)) {snprintf(result,sizeof(result),"Wait for video cleanup before music playback.");return 0;}
    snprintf(device,sizeof(device),"xenonfin-%02x%02x%02x%02x%02x%02x",netif.hwaddr[0],netif.hwaddr[1],netif.hwaddr[2],netif.hwaddr[3],netif.hwaddr[4],netif.hwaddr[5]);
    return xf_music_play(&music,&endpoint,device,token,user_id,it,&browser.current,browser.current.start+browser.current.selected,browser.page.total,now);
}
static void detail_line(const char *label, const char *value) {
    if (*value) { printf("  %s", label); cell(value, text_width > strlen(label) ? text_width - (unsigned)strlen(label) : 1); printf("\n"); }
}
static void draw_browser(void) {
    unsigned i;
    if (!authenticated) { printf("\n  Sign in first, then A loads your accessible libraries.\n"); return; }
    printf("\n  "); cell(browser.current.title, text_width - 10);
    if (browser.current.view >= XF_ARTISTS && browser.current.view <= XF_TRACKS)
        printf(" / %s", browser.current.view == XF_ARTISTS ? "Artists" : browser.current.view == XF_ALBUMS ? "Albums" : "Tracks");
    printf("\n");
    if (browser.details) {
        const xf_item *it = &browser.detail;
        unsigned count = xf_text_lines(it->overview, text_width), rows;
        int remaining;
        wrapped(it->name, 0, 3);
        detail_line("Type: ", it->type); detail_line("Media: ", it->media); detail_line("Library type: ", it->collection);
        printf("  Year: %u  Runtime: %llu:%02llu  Played: %s\n", it->year,
            (unsigned long long)(it->ticks / 600000000), (unsigned long long)(it->ticks / 10000000 % 60), it->played ? "yes" : "no");
        if (it->index || it->season) printf("  Season: %u  Episode / track: %u\n", it->season, it->index);
        if (it->position) printf("  Resume position: %llu seconds\n", (unsigned long long)(it->position / 10000000));
        if (it->children) printf("  Children: %u\n", it->children);
        detail_line("Series: ", it->series); detail_line("Artist: ", it->artist); detail_line("Album: ", it->album);
        detail_line("Rating: ", it->rating); detail_line("Genres: ", it->genres);
        printf("  Overview (Up/Down scrolls):\n");
        remaining = console_get_cursor_max_y() - xf_console_cursor_y() - 5;
        rows = remaining > 0 ? (unsigned)remaining : 1; if (rows > 8) rows = 8;
        if (count <= rows) browser.detail_scroll = 0;
        else if (browser.detail_scroll > count - rows) browser.detail_scroll = count - rows;
        wrapped(it->overview, browser.detail_scroll, rows);
        if (!it->overview[0]) printf("  No overview supplied by server.\n");
        if(!strcmp(it->type,"Audio"))printf("  A: play music   B: list   Back/View: now playing\n");
        else if(!strcmp(it->type,"Movie") || !strcmp(it->type,"Episode"))printf("  A: play Jellyfin (mode set in Video section) B: list\n");
        else printf("  B: return to list.\n");
    } else {
        if (!browser.loaded) printf("  A: load accessible Jellyfin libraries.\n");
        else if (!browser.page.count) printf("  Empty page / library. X refreshes; B goes back.\n");
        printf("    "); cell("TITLE", text_width - 20); printf(" | "); cell("TYPE", 14); printf("\n");
        for (i = 0; i < browser.page.count; ++i) {
            printf("  %c ", i == browser.current.selected ? '>' : ' '); cell(browser.page.items[i].name, text_width - 20);
            printf(" | "); cell(browser.current.view == XF_VIEWS && browser.page.items[i].collection[0] ? browser.page.items[i].collection : browser.page.items[i].type, 14); printf("\n");
        }
        if (browser.page.count) {
            const xf_item *it = &browser.page.items[browser.current.selected];
            printf("  Items %u-%u of %u  |  Year %u  Runtime %llu min\n", browser.current.start + 1,
                browser.current.start + browser.page.count, browser.page.total, it->year, (unsigned long long)(it->ticks / 600000000));
            if (!strcmp(it->type, "Audio")) {
                detail_line("Artist: ", it->artist); detail_line("Album: ", it->album);
                printf("  Track %u | %llu:%02llu | A: play | Y: details\n", it->index,
                    (unsigned long long)(it->ticks / 600000000), (unsigned long long)(it->ticks / 10000000 % 60));
            } else { printf("  Selected: "); cell(it->name, text_width - 10); printf("\n"); }
        }
        printf("  Up/Down: select  Left/Right: page  A: open  Y: details\n  B: back  X: refresh  Start: music artists/albums/tracks\n");
    }
}
static void draw_player(void) {
    uint64_t seconds = xf_music_position(&music) / 10000000;
    char line[180];
    printf("\n"); wrapped(xf_music_status(&music), 0, 2);
    detail_line("Title: ", music.track.name); detail_line("Artist: ", music.track.artist); detail_line("Album: ", music.track.album);
    printf("  Track %u | %llu:%02llu / %llu:%02llu\n", music.track.index,
        (unsigned long long)(seconds / 60), (unsigned long long)(seconds % 60),
        (unsigned long long)(music.track.ticks / 600000000), (unsigned long long)(music.track.ticks / 10000000 % 60));
    snprintf(line, sizeof(line), "Stream: HTTP %d | %s | input %u Hz / %u channel(s)", music.stream_status,
        music.source.transcode ? "server MP3 transcode" : "MP3 direct", music.decoder.rate, music.decoder.channels);
    wrapped(line, 0, 2);
    printf("  Output: 48000 Hz stereo S16 little endian\n  Decoded MP3 frames: %u | PCM played: %llu frames\n", music.decoder.mp3_frames, (unsigned long long)music.played);
    printf("  Buffers: compressed %u/128 KiB, PCM %u/128 KiB\n  Rebuffer events: %u | Last reporting HTTP: %d\n",
        (unsigned)(music.decoder.input_size / 1024), (unsigned)(music.decoder.pcm_size / 1024), music.underruns, music.report_status);
    wrapped(music.message, 0, 3); if (music.report_message[0]) wrapped(music.report_message, 0, 2);
    printf("\n  A/Start: pause/resume   X: stop\n  Left/Right: previous/next track\n  B: library (music continues)   LB/RB: sections\n");
}
static void draw_keyboard(void) {
    size_t i;
    printf("\n  %s\n\n  ", edit_field == 1 ? "Server IPv4[:port] (default 8096)" : edit_field == 2 ? "Jellyfin username" : "Jellyfin password (masked)");
    if (keyboard.secret) for (i = 0; keyboard.value[i]; ++i) xf_console_putch('*');
    else safe_print(keyboard.value, XF_TEXT_LIMIT);
    printf("_\n\n");
    for (i = 0; i < xf_keyboard_count(&keyboard); ++i) {
        char label[8]; const char *key = xf_keyboard_key(&keyboard, i, label);
        if (!(i % 10)) printf("  ");
        printf(i == keyboard.selected ? "[%5s] " : " %5s  ", key);
        if (i % 10 == 9) printf("\n");
    }
    printf("\n\n  D-pad/stick: move  A: type  X: delete  Y: symbols\n  SHIFT: uppercase  Start/DONE: accept  B: cancel\n  Max 128 printable ASCII characters.\n");
}
static void draw_network(void) {
    printf("\n  %s\n", xf_network_status());
    if (xf_network_available()) {
        char ip[16], mask[16], gateway[16], server[16], offer[16];
        struct dhcp *d = netif.dhcp;
        ipaddr_ntoa_r(&netif.ip_addr, ip, sizeof(ip)); ipaddr_ntoa_r(&netif.netmask, mask, sizeof(mask)); ipaddr_ntoa_r(&netif.gw, gateway, sizeof(gateway));
        printf("  Driver link flag: %s   Interface up: %s\n", network_link_up() ? "UP" : "DOWN", netif_is_up(&netif) ? "yes" : "no");
        printf("  IP: %s   Netmask: %s   Gateway: %s\n", ip, mask, gateway);
        printf("  DHCP state: %s (%d)\n", xf_dhcp_state(), d ? d->state : -1);
        printf("  DHCP managed: %s   ACK lease verified: %s\n", netif.flags & NETIF_FLAG_DHCP ? "yes" : "no", xf_network_ready() ? "YES" : "NO");
        printf("  TX DMA busy returns (nonblocking): %u\n", xf_driver_busy_count());
        if (d) {
            ipaddr_ntoa_r(&d->server_ip_addr, server, sizeof(server)); ipaddr_ntoa_r(&d->offered_ip_addr, offer, sizeof(offer));
            printf("  DHCP server: %s   ACK/offer IP: %s\n  Lease: %lu seconds   Retries: %u\n", server, offer, (unsigned long)d->offered_t0_lease, d->tries);
        }
        network_print_config(); /* Retain original diagnostic including MAC display. */
    }
    printf("  A: initialise/retry DHCP. There is no automatic static-IP fallback.\n");
}
static void start_video(int gpu) {
    char path[128]; xf_video_backend backend;
    if(xf_jvideo_active(&jvideo)) {snprintf(video.message,sizeof(video.message),"Wait for server video cleanup.");return;}
    xf_jvideo_forget(&jvideo);
    if(xf_music_active(&music) || xf_sound_diag_active(&sound_test) || xenon_sound_get_unplayed()) {
        snprintf(video.message,sizeof(video.message),"Stop music on Now playing first; local video test has no audio."); return;
    }
    if(http.active) { snprintf(video.message,sizeof(video.message),"Wait for the current HTTP request before mounting USB."); return; }
    if(!xf_video_local_path(path,sizeof(path),video.message,sizeof(video.message))) return;
    xf_video_native_backend(&backend,gpu); xf_video_start(&video,path,&backend);
}
static void start_video_colours(int gpu) {
    xf_video_backend backend;
    if(xf_jvideo_active(&jvideo))return;
    xf_jvideo_forget(&jvideo);
    if(xf_music_active(&music) || xf_sound_diag_active(&sound_test) || xenon_sound_get_unplayed() || http.active) {
        snprintf(video.message,sizeof(video.message),"Stop music and wait for HTTP before the colour test."); return;
    }
    xf_video_native_backend(&backend,gpu); xf_video_start_colours(&video,&backend);
}
static double video_avg(uint64_t us, unsigned frames) { return frames?(double)us/frames/1000:0; }
static void video_line(const char *format, ...) {
    char text[256], cell[128]; va_list arguments;
    va_start(arguments,format); vsnprintf(text,sizeof(text),format,arguments); va_end(arguments);
    xf_text_cell(cell,sizeof(cell),text,text_width); printf("  %s\n",cell);
}
static void video_save_trace(void) {
    char path[128],source[128],error[180];const char *colon;
    if(xf_video_active(&video) || xf_music_active(&music) || xf_sound_diag_active(&sound_test) || xenon_sound_get_unplayed()) {snprintf(video.message,sizeof(video.message),"Stop video/music before saving CSV.");return;}
    snprintf(source,sizeof(source),"%s",video.path);
    if(!source[0] && !xf_video_local_path(source,sizeof(source),error,sizeof(error))) {
        snprintf(video.message,sizeof(video.message),"CSV needs the FAT USB test clip present.");return;
    }
    colon=strchr(source,':');
    if(!colon) {snprintf(video.message,sizeof(video.message),"No mounted USB volume for CSV.");return;}
    snprintf(path,sizeof(path),"%.*s/xenonfin-video.csv",(int)(colon-source+1),source);
    {int ok=xf_video_export(&video,path);
     snprintf(path,sizeof(path),"%.*s/xenonfin-av.csv",(int)(colon-source+1),source);
     ok=xf_jvideo_export(&jvideo,path) && ok;
     snprintf(video.message,sizeof(video.message),"%s",ok?"Saved xenonfin-video.csv + xenonfin-av.csv at USB root.":"CSV write failed; keep USB inserted.");}
}
static void pause_video(uint32_t now) {
    if(xf_jvideo_active(&jvideo))xf_jvideo_pause(&jvideo,now);else xf_video_pause(&video,now);
}
static void draw_video(void) {
    unsigned x,y,w,h,footer;
    const xf_video_render_stats *m=video.backend.stats;
    if(jvideo.state!=XF_JV_IDLE)video_line("A/Start pause X stop B back L/R stats | Mode %c %s",jvideo.mode==XF_AV_VIDEO_ONLY?'A':jvideo.mode==XF_AV_AUDIO_ONLY?'B':'C',jvideo.title);
    else video_line("A CPU/pause Y GPU X stop B back L/R stats LT late RT sync");
    video_line("%s %ux%u %.2f FPS | %s | %llu:%02llu | %u/16",video.codec->name,video.info.width,video.info.height,video.info.fps,
        xf_jvideo_active(&jvideo) && jvideo.state!=XF_JV_STOPPING?xf_jvideo_state_name(jvideo.av_state):jvideo.state==XF_JV_NEGOTIATING?"NEGOTIATING":jvideo.state==XF_JV_STOPPING?"STOPPING":video.state==XF_VIDEO_ERROR || jvideo.state==XF_JV_ERROR?"ERROR":
        video.paused || (jvideo.pause_requested && jvideo.audio.enabled && !jvideo.audio.queued)?"PAUSED":jvideo.pause_requested?"PAUSING":video.buffering?"BUFFERING":video.state==XF_VIDEO_LOADING?"LOADING":video.state==XF_VIDEO_PLAYING?"PLAYING":video.state==XF_VIDEO_ENDED?"END":"STOPPED",
        (unsigned long long)(video.timeline_us/60000000),(unsigned long long)((video.timeline_us/1000000)%60),video_metrics_page+1);
    video_line("%s",jvideo.state!=XF_JV_IDLE?jvideo.message:video.message);
    if(video.state==XF_VIDEO_LOADING && !video.streaming) video_line("File: %u / %u bytes (maximum 8 MiB)",(unsigned)video.loaded,(unsigned)video.size);
    if(!xf_framebuffer_video_area(&x,&y,&w,&h,&footer)) return;
    while((unsigned)xf_console_cursor_y()<footer) printf("\n");
    video_line("FPS %u.%03u avg %.2f | dec %u shown %u drop %u",video.fps_milli/1000,video.fps_milli%1000,
        video.timeline_us && video.rendered>1?(double)(video.rendered-1)*1000000/video.timeline_us:0,video.decoded,video.rendered,video.dropped);
    if(video_metrics_page==0) {
        video_line("ms D %.2f Cv %.2f Up %.2f Cmd %.2f",video_avg(video.decode_us,video.decoded),
            m?video_avg(m->conversion_us,m->prepared):0,m?video_avg(m->upload_us,m->prepared):0,m?video_avg(m->commands_us,m->prepared):0);
        video_line("GPU<=%.2f poll-gap %.2f Copy %.2f VW %.2f ms",m?video_avg(m->gpu_observed_us,m->gpu_completed):0,
            m?video_avg(m->gpu_poll_gap_us,m->gpu_completed):0,m?video_avg(m->copy_us,m->presented):0,video_avg(video.sync.wait_us,video.rendered));
        video_line("Q %u/%u peak %u prep %u | next sync %u late %.1f ms",video.count,XF_VIDEO_QUEUE,video.max_occupancy,video.pending,video.next_sync_mode,(double)video.next_late_threshold_us/1000);
    } else if(video_metrics_page==1) {
        video_line("Drops late %u overflow %u decode %u gpu %u eos %u",video.drop_reason[0],video.drop_reason[1],video.drop_reason[2],video.drop_reason[3],video.drop_reason[4]);
        video_line("Events: decode fault %u GPU stall %u EOF %u full %u",video.decode_failures,video.gpu_stalls,video.eof_events,video.full_ticks);
        video_line("Drop late avg %.2f p95 %.2f max %.2f ms",video_avg(video.drop_lateness.sum,video.drop_lateness.count),(double)xf_time_p95(&video.drop_lateness)/1000,(double)video.drop_lateness.max/1000);
    } else if(video_metrics_page==2) {
        video_line("Clock %.3f PTS %.3f drift %+.2f ms | back %u",(double)video.timeline_us/1000000,(double)video.last_present_pts/1000000,(double)video.drift_us/1000,video.clock_faults);
        video_line("Late avg %.2f p95 %.2f max %.2f ms",video_avg(video.lateness.sum,video.lateness.count),(double)xf_time_p95(&video.lateness)/1000,(double)video.lateness.max/1000);
        video_line("Sync %u sampled %.1f Hz fault %u halfReject %u",video.sync.mode,video.sync.period_us?1000000.0/video.sync.period_us:0,video.sync.faults,video.sync.half_rejected);
    } else if(video_metrics_page==3) {
        video_line("Proc<= avg %.2f p95 %.2f max %.2f ms",video_avg(video.frame_span.sum,video.frame_span.count),(double)xf_time_p95(&video.frame_span)/1000,(double)video.frame_span.max/1000);
        video_line("CPU/frame avg %.2f p95 %.2f max %.2f ms",video_avg(video.processing.sum,video.processing.count),(double)xf_time_p95(&video.processing)/1000,(double)video.processing.max/1000);
        video_line("Interval avg %.2f p95 %.2f max %.2f ms",video_avg(video.intervals.sum,video.intervals.count),(double)xf_time_p95(&video.intervals)/1000,(double)video.intervals.max/1000);
    } else if(video_metrics_page==4) {
        unsigned seen=0,i,limit=video.trace_count>XF_VIDEO_TRACE?XF_VIDEO_TRACE:video.trace_count;
        for(i=0;i<limit && seen<3;++i) {
            const xf_video_trace *t=&video.trace[(video.trace_count-1-i)%XF_VIDEO_TRACE];
            if(t->reason<XF_DROP_REASONS) {video_line("Drop reason %u PTS %.3f late %.2f work %.2f ms",t->reason,(double)t->pts/1000000,(double)t->lateness/1000,(double)t->processing/1000);++seen;}
        }
        if(!seen) video_line("No drops logged. R-stick saves CSV after END/STOP.");
        if(seen<3) video_line("Trace %u overwritten %u cancel %u starve %u",video.trace_count,video.trace_overwritten,video.cancelled,video.starved);
    } else if(video_metrics_page==5) {
        video_line("UI avg %.2f p95 %.2f max %.2f ms",video_avg(video.ui_times.sum,video.ui_times.count),(double)xf_time_p95(&video.ui_times)/1000,(double)video.ui_times.max/1000);
        video_line("Loop avg %.2f p95 %.2f max %.2f ms",video_avg(video.loop_times.sum,video.loop_times.count),(double)xf_time_p95(&video.loop_times)/1000,(double)video.loop_times.max/1000);
        video_line("Wait %.2f ms starve %u Qfull %u cancel %u",video_avg(video.wait_us,video.loop_count),video.starved,video.full_ticks,video.cancelled);
    } else if(video_metrics_page==6) {
        video_line("HTTP progressive MPEG-PS | %u | transcode %s",jvideo.stream_status,jvideo.source.transcoding?(video.decoded?"MPEG1 verified":"requested"):"no");
        video_line("Net %u kbit/s | PS %u/%u ES %u/%u bytes",jvideo.kbps,(unsigned)jvideo.ps.count,XF_PS_RING,(unsigned)xf_mpeg1_stream_used(),XF_VIDEO_ES_BUFFER);
        video_line("Timer %llu:%02llu / %llu:%02llu | underruns %u",(unsigned long long)(video.timeline_us/60000000),(unsigned long long)((video.timeline_us/1000000)%60),
            (unsigned long long)(jvideo.source.runtime/600000000),(unsigned long long)((jvideo.source.runtime/10000000)%60),video.underruns);
    } else if(video_metrics_page==7) {
        video_line("Source %s / %s / %s %ux%u (%u sources)",jvideo.source.container,jvideo.source.video_codec,jvideo.source.audio_codec,jvideo.source.width,jvideo.source.height,jvideo.source.sources);
        video_line("Net p95/max %.2f/%.2f Demux %.2f/%.2f ms",(double)xf_time_p95(&jvideo.network_times)/1000,(double)jvideo.network_times.max/1000,
            (double)xf_time_p95(&jvideo.demux_times)/1000,(double)jvideo.demux_times.max/1000);
        if(jvideo.report_message[0])video_line("%s",jvideo.report_message);
        else video_line("Reports start/progress/stop/clean %u/%u/%u/%u | errs %u",jvideo.start_status,jvideo.progress_status,jvideo.stop_status,jvideo.cleanup_status,jvideo.report_errors);
    } else if(video_metrics_page==8) {
        video_line("Audio %s %u Hz %uch -> PCM 48000 Hz stereo",jvideo.audio.decoder.mp3_frames?"MP3 verified":jvideo.audio.enabled?"validating":"none",jvideo.audio.decoder.rate,jvideo.audio.decoder.channels);
        video_line("PCM %u/%u DMA %u/%u ES %u/%u",(unsigned)jvideo.audio.decoder.pcm_size,XF_PCM_CAP,jvideo.audio.queued,XF_AV_DMA_BYTES,(unsigned)jvideo.audio.decoder.input_size,XF_COMPRESSED_CAP);
        video_line("Audio underruns %u gaps %u | PES A/V %llu/%llu",jvideo.audio.underruns,jvideo.audio.gap_events,(unsigned long long)jvideo.ps.audio_packets,(unsigned long long)jvideo.ps.packets);
    } else if(video_metrics_page==9) {
        uint64_t position=video.timeline_us;
        if(jvideo.audio.enabled)position=jvideo.audio.played*1000000/48000;
        if(jvideo.audio.enabled && xf_jvideo_active(&jvideo))xf_av_audio_clock(&jvideo.audio,&position);
        video_line("Audio %.3fs | V-A %+.2f ms maxlate %.2f",(double)position/1000000,(double)((int64_t)video.last_present_pts-(int64_t)position)/1000,(double)video.lateness.max/1000);
        video_line("PTS A-V %+.2f markerr %+.2f ms | late PCM %llu",(double)jvideo.audio.initial_offset_us/1000,(double)jvideo.audio.timestamp_error_us/1000,(unsigned long long)jvideo.audio.late_frames);
        video_line("MP3 avg/max %.2f/%.2f ms | played %llu sent %llu",video_avg(jvideo.audio_times.sum,jvideo.audio_times.count),(double)jvideo.audio_times.max/1000,(unsigned long long)jvideo.audio.played,(unsigned long long)jvideo.audio.submitted);
    } else if(video_metrics_page==10) {
        video_line("%s: %s",xf_jvideo_state_name(jvideo.av_state),jvideo.wait_reason);
        video_line("Hold %ums origin %s DMA/clock max %.2f/%.2fms",jvideo.stalled_ms,jvideo.error_origin[0]?jvideo.error_origin:"none",(double)jvideo.dma_times.max/1000,(double)jvideo.clock_times.max/1000);
        video_line("Refill avg/max %.2f/%.2f gap %.2f ms",video_avg(jvideo.refill_times.sum,jvideo.refill_times.count),(double)jvideo.refill_times.max/1000,(double)jvideo.max_service_gap_us/1000);
    } else if(video_metrics_page==11) {
        video_line("HTTP read %d calls %u EOF %u done %d bad %d",jvideo.stream.last_receive_result,jvideo.stream.receive_calls,jvideo.stream.eof_seen,jvideo.stream.done,jvideo.stream.response.failed);
        video_line("RX %llu PS %llu BP %d | DMA raw %u",(unsigned long long)jvideo.stream.received_size,(unsigned long long)jvideo.ps.received,jvideo.stream.backpressure,jvideo.audio.queued);
        video_line("DMA rptr %u last %u remain %u wrap %u",jvideo.dma_register&31,(jvideo.dma_register>>8)&31,jvideo.dma_register>>16,jvideo.dma_wraps);
    } else if(video_metrics_page==12) {
        video_line("PTS first30 [%u] Up/Down | last V/A %lld/%lld",av_stamp_page,(long long)jvideo.ps.last_video_pts,(long long)jvideo.ps.last_audio_pts);
        video_line("V offset %llu PTS90k %lld (%u saved)",(unsigned long long)jvideo.first_pts[0][av_stamp_page].offset,(long long)jvideo.first_pts[0][av_stamp_page].pts,jvideo.pts_count[0]);
        video_line("A offset %llu PTS90k %lld (%u saved)",(unsigned long long)jvideo.first_pts[1][av_stamp_page].offset,(long long)jvideo.first_pts[1][av_stamp_page].pts,jvideo.pts_count[1]);
    } else if(video_metrics_page==13) {
        unsigned i,n=jvideo.transition_count>3?3:jvideo.transition_count;
        video_line("Mode %c: L-stick changes when stopped | stages ms",jvideo.mode==XF_AV_VIDEO_ONLY?'A':jvideo.mode==XF_AV_AUDIO_ONLY?'B':'C');
        if(xf_jvideo_active(&jvideo)) {
            uint32_t now=xf_monotonic_ms();
            video_line("RX %u PS %u MP3 %u PCM %u",now-jvideo.stages[0].changed_at,now-jvideo.stages[1].changed_at,now-jvideo.stages[3].changed_at,now-jvideo.stages[4].changed_at);
            video_line("Submit %u Play %u Vdec %u Show %u",now-jvideo.stages[5].changed_at,now-jvideo.stages[6].changed_at,now-jvideo.stages[7].changed_at,now-jvideo.stages[9].changed_at);
            return;
        }
        for(i=jvideo.transition_count-n;i<jvideo.transition_count;++i) {
            const xf_av_transition *t=&jvideo.transitions[i%32];
            if(i+2>=jvideo.transition_count)video_line("State %s at %u ms",xf_jvideo_state_name(t->state),t->at);
        }
    } else if(video_metrics_page==14) {
        if(jvideo.rebuffer_count) {
            const xf_av_rebuffer *r=&jvideo.rebuffers[(jvideo.rebuffer_count-1)%XF_AV_REBUFFERS];
            video_line("Last rebuffer #%u: %s",jvideo.rebuffer_count,r->reason);
            video_line("PCM %u AES %u VES %u PS %u Vq %u",r->pcm,r->audio_es,r->video_es,r->ps,r->video_queue);
            video_line("Hold %ums service gap %.2fms submit age %.2fms",jvideo.rebuffer_open && xf_jvideo_active(&jvideo)?xf_monotonic_ms()-r->at:r->duration,(double)r->service_gap_us/1000,(double)r->submit_age_us/1000);
        } else {
            video_line("No unexpected rebuffer event recorded in this session.");
            video_line("L/R page 15 retains the last event after recovery.");
        }
    } else {
        const xf_network_metrics *n=xf_network_measurements();
        video_line("TCP wnd %u ann %u refused %u BPbytes %u",jvideo.stream.receive_window,jvideo.stream.announced_window,jvideo.stream.refused_bytes,jvideo.stream.blocked_bytes);
        video_line("Idle %ums state %u flags %x window retries %u",jvideo.stream.idle_age,jvideo.stream.tcp_state,jvideo.stream.tcp_flags,jvideo.stream.window_updates);
        video_line("RX batch %u peak %u yields %u gap %.2fms TXbusy %u",n->last_batch,n->max_batch,n->budget_yields,(double)n->max_gap_us/1000,xf_driver_busy_count());
    }
}
static void draw_audio(void) {
    uint32_t base=read32(0xea001600),status=read32(0xea001604),control=read32(0xea001608);
    extern int xenos_is_hdmi;
    printf("\n  A: 440 Hz stereo PCM test   Y: reset IDLE audio\n");
    printf("  Route %s | 48000 Hz S16LE stereo | resets %u\n",xenos_is_hdmi?"HDMI":"analogue",audio_resets);
    printf("  DMA base %08x control %08x status %08x\n",base,control,status);
    printf("  Queued %d free %d bytes | read %u last %u remain %u\n",xenon_sound_get_unplayed(),xenon_sound_get_free(),status&31,(status>>8)&31,status>>16);
    printf("  Tone frames %u/9600 submitted %u bytes state %u\n",sound_test.frames,sound_test.submitted,sound_test.state);
    wrapped(sound_test.message,0,3);
}
static void draw(int selected) {
    int i;
    unsigned width;
    console_get_dimensions(&width, NULL); text_width = width > 6 ? width - 6 : 32;
    if (text_width > 100) text_width = 100;
    console_set_colors(0xff101624, 0xffe6eaf4); xf_console_begin();
    printf("\n  XENONFIN 0.7.2 - XeLL / Jellyfin video\n");
    if (editing) { draw_keyboard(); wrapped(result, 0, 3); xf_console_end(); return; }
    if (selected >= 6) printf("  > %s (LB/RB: sections; Back/View: player)\n", sections[selected]);
    else for (i = 0; i < SECTION_COUNT; ++i) printf("  %c %s\n", i == selected ? '>' : ' ', sections[i]);
    if (selected < 6) printf("\n  LB/RB: sections  D-pad: navigate  A: select  B: back\n");
    if (selected == 0) printf("\n  Graphics initialized; controller input is polled.\n  Hardware boot, rendering, navigation and audio confirmed by user.\n  Text: changed regions; GPU flip wait disabled.\n");
    if (selected == 1) draw_network();
    if (selected == 2) draw_audio();
    if (selected == 3) printf("\n  XeLL ELF; no XEX conversion.\n  Native raw lwIP TCP: no socket or DNS layer, no TLS.\n  HTTP connect/idle timeout 8s; total request timeout 20s.\n  DHCP and HTTP continue while navigating/typing.\n  Server/username/token are RAM-only; password wiped on submit.\n  Local silent MPEG-1 test: select Local video test.\n");
    if (selected == 4) {
        printf("\n  Server: "); safe_print(address[0] ? address : "not set", XF_TEXT_LIMIT);
        printf("\n  A: GET /System/Info/Public   Y: edit IP/port   X: body/name\n  HTTP is plaintext: use only your trusted LAN.\n");
        if (http.response.status && !http.sensitive) printf("  HTTP status: %d   Body bytes: %u\n", http.response.status, (unsigned)http.response.body_size);
        if (server_verified && !show_body) { printf("  Server name: "); safe_print(server_name, 100); printf("\n  Version: "); safe_print(version, 60); printf("\n"); }
        if (show_body && !http.sensitive && !http.active) {
            size_t offset;
            printf("  Public response (first 420 bytes, controls sanitized):\n");
            for (offset = 0; offset < http.response.body_size && offset < 420; offset += 70) {
                printf("  "); safe_print(http.response.body + offset, 70); printf("\n");
            }
        }
    }
    if (selected == 5) {
        const char *rows[] = {"Username", "Password (masked entry)", "Sign in (HTTP trusted LAN only)", "Log out / erase RAM token"};
        printf("\n  Test the server successfully before signing in.\n");
        for (i = 0; i < 4; ++i) printf("  %c %s\n", i == login_selected ? '>' : ' ', rows[i]);
        printf("  Username: "); safe_print(username, 100);
        printf("\n  Password: %s\n", password[0] ? "entered (hidden)" : "empty / cleared after submit");
        if (authenticated) { printf("  Authenticated user: "); safe_print(user_name[0] ? user_name : username, 100); printf("\n  Token received and kept in RAM only.\n"); }
    }
    if (selected == 6) draw_browser();
    if (selected == 7) draw_player();
    if (selected == 8) draw_video();
    if (selected == 4 || selected == 5 || selected == 6 || http.active) {
        wrapped(result, 0, 3);
        if (http.active) printf("  %s  Sent %u/%u bytes  Received %u bytes\n", http.connected ? "Waiting for HTTP" : "Connecting TCP", (unsigned)http.queued, (unsigned)http.request_size, (unsigned)http.received_size);
    }
    if (selected < 6) printf("\n  B exits outside keyboard/request/login/library/player.\n");
    xf_console_end();
}
static int audio_queued(void *context) {
    (void)context;
    if(jvideo.audio.enabled && xf_jvideo_active(&jvideo)) {
        /* Read-only diagnostic of the SAME register used by pinned sound.c.
         * Clock calculation still uses the public driver's unplayed count. */
        unsigned raw=read32(0xea001604);
        if(jvideo.dma_seen && (raw&31)<(jvideo.dma_register&31))++jvideo.dma_wraps;
        jvideo.dma_register=raw;jvideo.dma_seen=1;
    }
    return xenon_sound_get_unplayed();
}
static int audio_free(void *context) { (void)context; return xenon_sound_get_free(); }
static void audio_submit(void *context, const unsigned char *bytes, size_t size) { (void)context; xenon_sound_submit((void *)bytes, (int)size); }
int main(void) {
    struct controller_data_s pad, previous;
    int selected = 0, redraw = 1;
    char held = 0;
    uint32_t repeat_at = 0;
    xf_ui_clock ui_clock = {0, 0};
    xf_audio_backend audio = { audio_queued, audio_free, audio_submit, NULL };
    memset(&previous, 0, sizeof(previous)); xf_http_init(&http); xf_browser_init(&browser);
    xf_sound_diag_init(&sound_test,&audio);xf_music_init(&music, &audio); xf_video_init(&video,&xf_mpeg1_codec);xf_jvideo_init(&jvideo,&video);xf_jvideo_audio(&jvideo,&audio);
    /* Preserve the hardware-tested initialization order. */
    xenos_init(VIDEO_MODE_AUTO); console_init(); usb_init(); xenon_sound_init();
    if (!xf_console_init()) { printf("Unsupported framebuffer geometry; text compositor not started.\n"); return 1; }
    for (;;) {
        uint64_t loop_start=xf_monotonic_us();
        uint32_t now = (uint32_t)(loop_start/1000); char direction = 0, move = 0;
        xf_sound_diag_tick(&sound_test,now);
        xf_jvideo_service(&jvideo,now);
        service_network(now);
        xf_http_tick(&http, now, xf_network_ready());
        xf_music_tick(&music, now, xf_network_ready());
        xf_jvideo_tick(&jvideo,now,xf_network_ready());
        if(jvideo.mode!=XF_AV_AUDIO_ONLY || !xf_jvideo_active(&jvideo))xf_video_tick(&video,now);
        xf_jvideo_service(&jvideo,now);
        service_network(xf_monotonic_ms());
        if (music.session_expired || jvideo.session_expired) {
            xf_http_cancel(&http); processed = 1; logout(); snprintf(result, sizeof(result), "HTTP 401: playback session expired. Sign in again."); redraw = 1;
        }
        if (!processed && http.done && !http.active) { handle_result(); redraw = 1; }
        usb_do_poll(); memset(&pad, 0, sizeof(pad)); get_controller_data(&pad, 0);
        if (pad.left || pad.s1_x < -18000) direction = 'L';
        if (pad.right || pad.s1_x > 18000) direction = 'R';
        if (pad.up || pad.s1_y > 18000) direction = 'U';
        if (pad.down || pad.s1_y < -18000) direction = 'D';
        if (direction != held) { held = direction; repeat_at = now + 350; move = direction; }
        else if (direction && (int32_t)(now - repeat_at) >= 0) { move = direction; repeat_at = now + 150; }
        if (editing) {
            if (move) { xf_keyboard_input(&keyboard, move); redraw = 1; }
            if (pad.x && !previous.x) { xf_keyboard_input(&keyboard, 'X'); redraw = 1; }
            if (pad.y && !previous.y) { xf_keyboard_input(&keyboard, 'Y'); redraw = 1; }
            if ((pad.a && !previous.a && xf_keyboard_input(&keyboard, 'A')) || (pad.start && !previous.start)) { finish_edit(); redraw = 1; }
            else if (pad.a && !previous.a) redraw = 1;
            if (pad.b && !previous.b) { xf_wipe(&keyboard, sizeof(keyboard)); editing = 0; redraw = 1; }
        } else {
            int old_selected = selected;
            if (pad.rb && !previous.rb) { selected = (selected + 1) % SECTION_COUNT; redraw = 1; }
            if (pad.lb && !previous.lb) { selected = (selected + SECTION_COUNT - 1) % SECTION_COUNT; redraw = 1; }
            if (pad.back && !previous.back) { selected = 7; redraw = 1; }
            if (move == 'U' || move == 'D') {
                if (selected == 5) login_selected = (login_selected + (move == 'D' ? 1 : 3)) % 4;
                else if (selected == 6) {
                    if (browser.details) { if (move == 'D') ++browser.detail_scroll; else if (browser.detail_scroll) --browser.detail_scroll; }
                    else xf_browser_move(&browser, move == 'D' ? 1 : -1);
                }
                else if (selected != 7 && selected != 8) selected = (selected + (move == 'D' ? 1 : SECTION_COUNT-1)) % SECTION_COUNT;
                redraw = 1;
            }
            if (old_selected != 6 && selected == 6 && !browser.loaded && authenticated) browse_request(XF_ROOT, now);
            if (selected == 6 && (move == 'L' || move == 'R')) { browse_request(move == 'R' ? XF_NEXT : XF_PREVIOUS, now); redraw = 1; }
            if (selected == 7 && (move == 'L' || move == 'R')) { xf_music_skip(&music, move == 'R' ? 1 : -1, now); redraw = 1; }
            if (pad.b && !previous.b) {
                if (http.active) { xf_http_cancel(&http); handle_result(); }
                else if (selected == 6) {
                    if (!browser.details && !browser.depth) selected = 5;
                    else browse_request(XF_BACK, now);
                }
                else if (selected == 5) selected = 4;
                else if (selected == 7) selected = 6;
                else if (selected == 8) { xf_jvideo_stop(&jvideo,now);xf_video_stop(&video); selected=6; }
                else { xf_http_cancel(&http); logout(); xf_wipe(password, sizeof(password)); xf_video_stop(&video); xf_console_shutdown(); return 0; }
                redraw = 1;
            }
            if(pad.y && !previous.y && selected==2) {
                if(xf_music_active(&music) || xf_jvideo_active(&jvideo) || xf_sound_diag_active(&sound_test))
                    snprintf(sound_test.message,sizeof(sound_test.message),"Reset blocked while playback/tone owns audio. Stop and wait first.");
                else {
                    xenon_sound_init();++audio_resets;xf_sound_diag_init(&sound_test,&audio);
                    snprintf(sound_test.message,sizeof(sound_test.message),"Audio reset complete. Press A; check raw control and DMA progress.");
                }
                redraw=1;
            }
            if (pad.y && !previous.y && selected == 4) { begin_edit(1); redraw = 1; }
            if (pad.x && !previous.x && selected == 4) { show_body = !show_body; redraw = 1; }
            if (selected == 6) {
                if (pad.y && !previous.y) { browse_request(XF_DETAILS, now); redraw = 1; }
                if (pad.x && !previous.x) { browse_request(XF_REFRESH, now); redraw = 1; }
                if (pad.start && !previous.start) { browse_request(XF_MUSIC, now); redraw = 1; }
            }
            if (selected == 7) {
                if (pad.x && !previous.x) { xf_music_stop(&music, now); redraw = 1; }
                if ((pad.start && !previous.start) || (pad.a && !previous.a)) { xf_music_pause(&music); redraw = 1; }
            }
            if(selected==8) {
                if(move=='L' || move=='R') { video_metrics_page=(video_metrics_page+(move=='R'?1:15))%16; redraw=1; }
                if(!xf_video_active(&video) && !xf_jvideo_active(&jvideo)) {
                    if(pad.rt>128 && previous.rt<=128) {xf_video_options(&video,(video.next_sync_mode+1)%3,video.next_late_threshold_us);redraw=1;}
                    if(pad.lt>128 && previous.lt<=128) {
                        uint64_t late=video.next_late_threshold_us==0?66667:video.next_late_threshold_us==66667?100000:video.next_late_threshold_us==100000?200000:0;
                        xf_video_options(&video,video.next_sync_mode,late);redraw=1;
                    }
                    if(pad.s1_z && !previous.s1_z) {xf_jvideo_mode(&jvideo,(jvideo.mode+1)%3);redraw=1;}
                    if(pad.s2_z && !previous.s2_z) {video_save_trace();if(jvideo.state!=XF_JV_IDLE)snprintf(jvideo.message,sizeof(jvideo.message),"%s",video.message);redraw=1;}
                }
                if((move=='U' || move=='D') && xf_jvideo_active(&jvideo) && video_metrics_page==12) {av_stamp_page=(av_stamp_page+(move=='D'?1:29))%30;redraw=1;}
                if((move=='U' || move=='D') && !xf_video_active(&video) && !xf_jvideo_active(&jvideo)) { start_video_colours(move=='D'); redraw=1; }
                if(pad.x && !previous.x) { xf_jvideo_stop(&jvideo,now);xf_video_stop(&video); redraw=1; }
                if(pad.start && !previous.start) { pause_video(now); redraw=1; }
                if(pad.y && !previous.y && !xf_video_active(&video) && !xf_jvideo_active(&jvideo)) { if(jvideo.state!=XF_JV_IDLE)start_network_video(&selected_video,now);else start_video(1);redraw=1; }
            }
            if (pad.a && !previous.a) {
                if(selected==8) { if(xf_video_active(&video)) pause_video(now); else if(xf_jvideo_active(&jvideo))pause_video(now); else {if(jvideo.state!=XF_JV_IDLE)start_network_video(&selected_video,now);else start_video(0);} }
                if (selected == 1) {
                    xf_http_cancel(&http); handle_result(); processed = 1;
                    printf("\n  Initializing existing NIC driver (12s PHY watchdog).\n  DHCP negotiation will continue cooperatively afterwards.\n");
                    if (!xf_network_start(xf_monotonic_ms())) snprintf(result, sizeof(result), "Network start failed; inspect DHCP diagnostics.");
                }
                if (selected == 2) {
                    if(xf_music_active(&music) || xf_jvideo_active(&jvideo)) {
                        xf_music_stop(&music,now);xf_jvideo_stop(&jvideo,now);
                        snprintf(sound_test.message,sizeof(sound_test.message),"Stopping playback. Press A after cleanup to test the tone.");
                    } else xf_sound_diag_start(&sound_test,0,now);
                }
                if (selected == 4 && !http.active) { if (!server_valid) begin_edit(1); else start_request(0, now); }
                if (selected == 5 && !http.active) {
                    if (login_selected < 2) begin_edit(login_selected + 2);
                    else if (login_selected == 2) start_request(1, now);
                    else { logout(); xf_wipe(password, sizeof(password)); snprintf(result, sizeof(result), "Logged out; local token erased."); }
                }
                if (selected == 6) {
                    int target=play_selected(now);
                    if(target>0) selected=target==2?8:7;
                    else if(!target) browse_request(browser.loaded ? XF_OPEN : XF_ROOT, now);
                }
                redraw = 1;
            }
        }
        if(selected!=8 && xf_jvideo_active(&jvideo))xf_jvideo_stop(&jvideo,now);
        if(selected!=8 && (video.backend_started || video.state==XF_VIDEO_LOADING)) { xf_video_stop(&video); redraw=1; }
        /* Presentation never waits on GPU acknowledgement, decoding or HTTP.
         * Dirty controller/data events persist while a flip is pending. Only
         * visible statistics get timer updates, at most four per second. */
        xf_video_service(&video);
        xf_jvideo_service(&jvideo,now);
        xf_console_poll(now);
        if (xf_console_ready() && (redraw || xf_video_ui_safe(&video)) && xf_ui_due(&ui_clock, now, redraw,
                !editing && (selected == 1 || selected == 2 || selected == 7 || selected == 8 || http.active))) {
            uint64_t ui_start=xf_monotonic_us();
            draw(selected); xf_ui_rendered(&ui_clock, now); redraw = 0;
            if(selected==8 && video.state==XF_VIDEO_PLAYING) { uint64_t cost=xf_monotonic_us()-ui_start;video.ui_us+=cost;++video.ui_frames;xf_time_add(&video.ui_times,cost); }
        }
        xf_video_service(&video);
        xf_jvideo_service(&jvideo,now);
        previous = pad;
        /* Receive-window ACKs and arrived packets need service between codec
         * calls, not just once per decoded/presented frame. Audio stays first. */
        service_network(xf_monotonic_ms());
        xf_jvideo_service(&jvideo,now);
        {
            uint64_t wait_start=xf_monotonic_us();
            unsigned wait=xf_video_wait_us(&video);
            /* Deadline-aware video waits run only after the priority PCM refill.
             * No sleeps occur inside the audio decode/output path. */
            if(wait) udelay((int)wait);
            if(video.state==XF_VIDEO_PLAYING && !video.paused && video.clock_started) {
                xf_time_add(&video.loop_times,wait_start-loop_start);video.loop_us+=wait_start-loop_start; video.wait_us+=xf_monotonic_us()-wait_start; ++video.loop_count;
            }
        }
    }
}
