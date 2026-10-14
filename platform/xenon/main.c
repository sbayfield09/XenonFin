/* Extend the hardware-tested XeLL diagnostic; graphics, USB/controller, sound,
 * Ethernet driver and the original four diagnostic pages remain in place. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <xenos/xenos.h>
#include <xenon_sound/sound.h>
#include <network/network.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <xenon_soc/xenon_power.h>
#include "native_net.h"
#include "driver_guard.h"
#include "browse.h"

static const char *sections[] = {
    "Graphics / controller", "Network / DHCP diagnostics", "Audio output",
    "Platform status", "Jellyfin server", "Jellyfin login", "Libraries"
};
static xf_http_client http;
static xf_browser browser;
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
uint32_t xf_monotonic_ms(void) {
    uint32_t high, low, check;
    /* Stable read across the lower timebase register's ~86-second rollover. */
    do {
        asm volatile ("mftbu %0" : "=r" (high));
        asm volatile ("mftbl %0" : "=r" (low));
        asm volatile ("mftbu %0" : "=r" (check));
    } while (high != check);
    return (uint32_t)((((uint64_t)high << 32) | low) / (PPC_TIMEBASE_FREQ / 1000));
}
static void logout(void) {
    authenticated = 0; xf_wipe(token, sizeof(token)); xf_wipe(user_id, sizeof(user_id)); xf_wipe(user_name, sizeof(user_name));
    xf_browser_init(&browser);
}
static void safe_print(const char *text, size_t max) {
    size_t i;
    for (i = 0; i < max && text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        /* Never interpret server-controlled terminal escapes/control bytes. */
        console_putch(c >= 32 && c < 127 ? c : '.');
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
static void detail_line(const char *label, const char *value) {
    if (*value) { printf("  %s", label); safe_print(value, 68); printf("\n"); }
}
static void draw_browser(void) {
    unsigned i;
    if (!authenticated) { printf("\n  Sign in first, then A loads your accessible libraries.\n"); return; }
    printf("\n  "); safe_print(browser.current.title, 65);
    if (browser.current.view >= XF_ARTISTS && browser.current.view <= XF_TRACKS)
        printf(" / %s", browser.current.view == XF_ARTISTS ? "Artists" : browser.current.view == XF_ALBUMS ? "Albums" : "Tracks");
    printf("\n");
    if (browser.details) {
        const xf_item *it = &browser.detail;
        for (i = 0; i < strlen(it->name); i += 68) { printf("  "); safe_print(it->name + i, 68); printf("\n"); }
        detail_line("Type: ", it->type); detail_line("Media: ", it->media); detail_line("Library type: ", it->collection);
        printf("  Year: %u  Runtime: %llu:%02llu  Played: %s\n", it->year,
            (unsigned long long)(it->ticks / 600000000), (unsigned long long)(it->ticks / 10000000 % 60), it->played ? "yes" : "no");
        if (it->index || it->season) printf("  Season: %u  Episode / track: %u\n", it->season, it->index);
        if (it->position) printf("  Resume position: %llu seconds\n", (unsigned long long)(it->position / 10000000));
        if (it->children) printf("  Children: %u\n", it->children);
        detail_line("Series: ", it->series); detail_line("Artist: ", it->artist); detail_line("Album: ", it->album);
        detail_line("Rating: ", it->rating); detail_line("Genres: ", it->genres);
        printf("  Overview (up to 768 bytes; Up/Down scrolls):\n");
        for (i = browser.detail_scroll * 68; i < strlen(it->overview) && i < (browser.detail_scroll + 4) * 68; i += 68) { printf("  "); safe_print(it->overview + i, 68); printf("\n"); }
        if (!it->overview[0]) printf("  No overview supplied by server.\n");
        printf("  B: return to list. Playback is not implemented.\n");
    } else {
        if (!browser.loaded) printf("  A: load accessible Jellyfin libraries.\n");
        else if (!browser.page.count) printf("  Empty page / library. X refreshes; B goes back.\n");
        for (i = 0; i < browser.page.count; ++i) {
            printf("  %c ", i == browser.current.selected ? '>' : ' '); safe_print(browser.page.items[i].name, 46);
            printf(" ["); safe_print(browser.current.view == XF_VIEWS && browser.page.items[i].collection[0] ? browser.page.items[i].collection : browser.page.items[i].type, 16); printf("]\n");
        }
        if (browser.page.count) {
            const xf_item *it = &browser.page.items[browser.current.selected];
            printf("  Items %u-%u of %u  |  Year %u  Runtime %llu min\n", browser.current.start + 1,
                browser.current.start + browser.page.count, browser.page.total, it->year, (unsigned long long)(it->ticks / 600000000));
            printf("  Selected: "); safe_print(it->name, 66); printf("\n");
        }
        printf("  Up/Down: select  Left/Right: page  A: open  Y: details\n  B: back  X: refresh  Start: music artists/albums/tracks\n");
    }
}
static void draw_keyboard(void) {
    size_t i;
    printf("\n  %s\n\n  ", edit_field == 1 ? "Server IPv4[:port] (default 8096)" : edit_field == 2 ? "Jellyfin username" : "Jellyfin password (masked)");
    if (keyboard.secret) for (i = 0; keyboard.value[i]; ++i) console_putch('*');
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
static void draw(int selected) {
    int i;
    console_set_colors(0xff101624, 0xffe6eaf4); console_clrscr();
    printf("\n  XENONFIN - XeLL / Jellyfin library browsing 0.3\n");
    printf("  Existing graphics/controller/audio/Ethernet diagnostics retained.\n");
    if (editing) { draw_keyboard(); printf("\n  "); safe_print(result, 150); printf("\n"); return; }
    if (selected == 6) printf("  > Libraries (LB/RB: other sections)\n");
    else for (i = 0; i < 7; ++i) printf("  %c %s\n", i == selected ? '>' : ' ', sections[i]);
    printf("\n  LB/RB: sections  D-pad: navigate  A: select  B: cancel/back\n");
    if (selected == 0) printf("\n  Graphics initialized; controller input is polled.\n  Hardware boot, rendering, navigation and audio confirmed by user.\n");
    if (selected == 1) draw_network();
    if (selected == 2) printf("\n  Press A for the existing 440 Hz stereo PCM tone.\n");
    if (selected == 3) printf("\n  XeLL ELF; no XEX conversion.\n  Native raw lwIP TCP: no socket or DNS layer, no TLS.\n  HTTP connect/idle timeout 8s; total request timeout 20s.\n  DHCP and HTTP continue while navigating/typing.\n  Server/username/token are RAM-only; password wiped on submit.\n  Video decoding is not part of this build.\n");
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
    if (selected == 4 || selected == 5 || selected == 6 || http.active) {
        printf("\n  "); safe_print(result, sizeof(result)); printf("\n");
        if (http.active) printf("  %s  Sent %u/%u bytes  Received %u bytes\n", http.connected ? "Waiting for HTTP" : "Connecting TCP", (unsigned)http.queued, (unsigned)http.request_size, (unsigned)http.received_size);
    }
    if (selected != 6) printf("\n  B exits to XeLL only outside keyboard/request/login/libraries.\n");
}
int main(void) {
    struct controller_data_s pad, previous;
    int selected = 0, redraw = 1;
    char held = 0;
    uint32_t repeat_at = 0, last_draw = 0;
    memset(&previous, 0, sizeof(previous)); xf_http_init(&http); xf_browser_init(&browser);
    /* Preserve the hardware-tested initialization order. */
    xenos_init(VIDEO_MODE_AUTO); console_init(); usb_init(); xenon_sound_init();
    for (;;) {
        uint32_t now = xf_monotonic_ms(); char direction = 0, move = 0;
        xf_network_tick(now); xf_http_tick(&http, now, xf_network_ready());
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
            if (pad.rb && !previous.rb) { selected = (selected + 1) % 7; redraw = 1; }
            if (pad.lb && !previous.lb) { selected = (selected + 6) % 7; redraw = 1; }
            if (move == 'U' || move == 'D') {
                if (selected == 5) login_selected = (login_selected + (move == 'D' ? 1 : 3)) % 4;
                else if (selected == 6) xf_browser_move(&browser, move == 'D' ? 1 : -1);
                else selected = (selected + (move == 'D' ? 1 : 6)) % 7;
                redraw = 1;
            }
            if (old_selected != 6 && selected == 6 && !browser.loaded && authenticated) browse_request(XF_ROOT, now);
            if (selected == 6 && (move == 'L' || move == 'R')) { browse_request(move == 'R' ? XF_NEXT : XF_PREVIOUS, now); redraw = 1; }
            if (pad.b && !previous.b) {
                if (http.active) { xf_http_cancel(&http); handle_result(); }
                else if (selected == 6) {
                    if (!browser.details && !browser.depth) selected = 5;
                    else browse_request(XF_BACK, now);
                }
                else if (selected == 5) selected = 4;
                else { xf_http_cancel(&http); logout(); xf_wipe(password, sizeof(password)); return 0; }
                redraw = 1;
            }
            if (pad.y && !previous.y && selected == 4) { begin_edit(1); redraw = 1; }
            if (pad.x && !previous.x && selected == 4) { show_body = !show_body; redraw = 1; }
            if (selected == 6) {
                if (pad.y && !previous.y) { browse_request(XF_DETAILS, now); redraw = 1; }
                if (pad.x && !previous.x) { browse_request(XF_REFRESH, now); redraw = 1; }
                if (pad.start && !previous.start) { browse_request(XF_MUSIC, now); redraw = 1; }
            }
            if (pad.a && !previous.a) {
                if (selected == 1) {
                    xf_http_cancel(&http); handle_result(); processed = 1;
                    printf("\n  Initializing existing NIC driver (12s PHY watchdog).\n  DHCP negotiation will continue cooperatively afterwards.\n");
                    if (!xf_network_start(xf_monotonic_ms())) snprintf(result, sizeof(result), "Network start failed; inspect DHCP diagnostics.");
                }
                if (selected == 2) xenon_tone(440, 200, XENON_TONE_AMPLITUDE_25);
                if (selected == 4 && !http.active) { if (!server_valid) begin_edit(1); else start_request(0, now); }
                if (selected == 5 && !http.active) {
                    if (login_selected < 2) begin_edit(login_selected + 2);
                    else if (login_selected == 2) start_request(1, now);
                    else { logout(); xf_wipe(password, sizeof(password)); snprintf(result, sizeof(result), "Logged out; local token erased."); }
                }
                if (selected == 6) browse_request(browser.loaded ? XF_OPEN : XF_ROOT, now);
                redraw = 1;
            }
        }
        /* Rate-limited diagnostics, not a continuously clearing/flickering console. */
        if ((uint32_t)(now - last_draw) >= 500 && (selected == 1 || http.active)) redraw = 1;
        if (redraw) { draw(selected); last_draw = now; redraw = 0; }
        previous = pad; udelay(10000);
    }
}
