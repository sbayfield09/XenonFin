#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void xf_wipe(void *memory, size_t size) {
    volatile unsigned char *p = (volatile unsigned char *)memory;
    while (size--) *p++ = 0;
}
static void message(char *out, size_t cap, const char *text) {
    if (cap) snprintf(out, cap, "%s", text);
}
int xf_endpoint_parse(const char *input, xf_endpoint *out, char *error, size_t cap) {
    const char *p = input;
    xf_endpoint parsed;
    unsigned n;
    int i;
    memset(&parsed, 0, sizeof(parsed)); parsed.port = 8096;
    if (!p) goto invalid;
    if (!strncmp(p, "https://", 8)) {
        message(error, cap, "HTTPS unavailable on this native build; no insecure TLS fallback"); return 0;
    }
    if (!strncmp(p, "http://", 7)) p += 7;
    for (i = 0; i < 4; ++i) {
        int digits = 0;
        n = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) goto invalid;
            n = n * 10 + (unsigned)(*p++ - '0');
        }
        if (!digits || n > 255) goto invalid;
        parsed.octets[i] = (uint8_t)n;
        if (i < 3 && *p++ != '.') goto invalid;
    }
    if (!parsed.octets[0] || parsed.octets[0] >= 224) goto invalid;
    if (*p == ':') {
        int digits = 0;
        ++p; n = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 5) goto invalid;
            n = n * 10 + (unsigned)(*p++ - '0');
        }
        if (!digits || !n || n > 65535) goto invalid;
        parsed.port = (uint16_t)n;
    }
    if (*p == '/') ++p;
    if (*p) goto invalid;
    snprintf(parsed.host, sizeof(parsed.host), "%u.%u.%u.%u", parsed.octets[0], parsed.octets[1], parsed.octets[2], parsed.octets[3]);
    *out = parsed; if (cap) *error = 0; return 1;
invalid:
    message(error, cap, "Enter IPv4[:port], e.g. 192.168.68.202:8096 (no DNS/base path)"); return 0;
}
int xf_json_quote(const char *input, char *out, size_t cap) {
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)input;
    if (cap < 3) return 0;
    out[used++] = '"';
    while (*p) {
        unsigned char c = *p++;
        if (c == '"' || c == '\\') {
            if (used + 2 >= cap) return 0;
            out[used++] = '\\'; out[used++] = (char)c;
        } else if (c < 32) {
            if (used + 6 >= cap) return 0;
            snprintf(out + used, cap - used, "\\u%04x", c); used += 6;
        } else {
            if (used + 1 >= cap) return 0;
            out[used++] = (char)c;
        }
    }
    out[used++] = '"'; out[used] = 0; return 1;
}
int xf_request(char *out, size_t cap, const xf_endpoint *ep, const char *device,
               const char *username, const char *password) {
    char user[XF_TEXT_LIMIT * 6 + 3], pass[XF_TEXT_LIMIT * 6 + 3];
    char body[XF_TEXT_LIMIT * 12 + 64];
    int length = -1, login = username != NULL;
    size_t i;
    xf_wipe(out, cap);
    /* Header DeviceId comes from the MAC, never from credentials or a response. */
    if (!device || strlen(device) > 64) return -1;
    for (i = 0; device[i]; ++i)
        if (!isalnum((unsigned char)device[i]) && device[i] != '-') return -1;
    body[0] = 0;
    if (login) {
        if (!password || !*username || strlen(username) > XF_TEXT_LIMIT || strlen(password) > XF_TEXT_LIMIT) goto done;
        if (!xf_json_quote(username, user, sizeof(user)) || !xf_json_quote(password, pass, sizeof(pass))) goto done;
        snprintf(body, sizeof(body), "{\"Username\":%s,\"Pw\":%s}", user, pass);
    }
    length = snprintf(out, cap,
        "%s %s HTTP/1.1\r\nHost: %s:%u\r\n"
        "Authorization: MediaBrowser Client=\"XenonFin\", Device=\"Xbox360 XeLL\", DeviceId=\"%s\", Version=\"0.6.0\"\r\n"
        "Accept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n%s",
        login ? "POST" : "GET", login ? "/Users/AuthenticateByName" : "/System/Info/Public", ep->host, ep->port,
        device, login ? "Content-Type: application/json\r\n" : "\r\n");
    if (length < 0 || (size_t)length >= cap) { length = -1; goto done; }
    if (login) {
        int added = snprintf(out + length, cap - (size_t)length, "Content-Length: %u\r\n\r\n%s", (unsigned)strlen(body), body);
        if (added < 0 || (size_t)added >= cap - (size_t)length) length = -1;
        else length += added;
    }
done:
    xf_wipe(user, sizeof(user)); xf_wipe(pass, sizeof(pass)); xf_wipe(body, sizeof(body));
    if (length < 0) xf_wipe(out, cap);
    return length;
}
int xf_api_request(char *out, size_t cap, const xf_endpoint *ep, const char *device,
                   const char *token, const char *method, const char *resource, const char *json, int audio) {
    size_t i; int n, added;
    xf_wipe(out, cap);
    if (!method || (strcmp(method, "GET") && strcmp(method, "POST") && strcmp(method, "DELETE")) ||
        (json && (strcmp(method, "POST") || strlen(json) > 2300))) return -1;
    if (!device || !*device || strlen(device) > 64 || !token || !*token || strlen(token) > 2048 ||
        !resource || resource[0] != '/' || resource[1] == '/' || strlen(resource) > 1500) return -1;
    for (i = 0; device[i]; ++i) if (!isalnum((unsigned char)device[i]) && device[i] != '-') return -1;
    for (i = 0; token[i]; ++i) {
        unsigned char c = (unsigned char)token[i];
        if (c < 33 || c > 126 || c == '"' || c == '\\') return -1;
    }
    for (i = 0; resource[i]; ++i) {
        unsigned char c = (unsigned char)resource[i];
        if (c < 33 || c > 126 || c == '#' || c == '\\') return -1;
    }
    n = snprintf(out, cap, "%s %s HTTP/1.1\r\nHost: %s:%u\r\n"
        "Authorization: MediaBrowser Client=\"XenonFin\", Device=\"Xbox360 XeLL\", DeviceId=\"%s\", Version=\"0.6.0\", Token=\"%s\"\r\n"
        "Accept: %s\r\nAccept-Encoding: identity\r\nConnection: close\r\n",
        method, resource, ep->host, ep->port, device, token, audio == 2 ? "video/mpeg,application/octet-stream" : audio ? "audio/mpeg,application/octet-stream" : "application/json");
    if (n < 0 || (size_t)n >= cap) goto fail;
    added = json ? snprintf(out + n, cap - (size_t)n, "Content-Type: application/json\r\nContent-Length: %u\r\n\r\n%s", (unsigned)strlen(json), json) :
                   snprintf(out + n, cap - (size_t)n, "%s\r\n", !strcmp(method, "POST") ? "Content-Length: 0\r\n" : "");
    if (added < 0 || (size_t)added >= cap - (size_t)n) goto fail;
    return n + added;
fail:
    xf_wipe(out, cap); return -1;
}
int xf_get_request(char *out, size_t cap, const xf_endpoint *ep, const char *device,
                   const char *token, const char *resource) {
    return xf_api_request(out, cap, ep, device, token, "GET", resource, NULL, 0);
}

int xf_lease_valid(int managed, int state, int up, uint32_t address, uint32_t offered, uint32_t lease) {
    /* lwIP 1.4.1: BOUND=10, RENEWING=5, REBINDING=4. Those latter two retain an ACKed lease. */
    return managed && up && (state == 10 || state == 5 || state == 4) && address && address == offered && lease;
}
int xf_public_info(const xf_response *r, char *server, size_t server_cap, char *version, size_t version_cap) {
    if (!r->complete || r->failed || r->status != 200 ||
        xf_json_string(r->body, r->body_size, "Version", version, version_cap) != 1 || !*version) return 0;
    if (xf_json_string(r->body, r->body_size, "ServerName", server, server_cap) < 0) return 0;
    if (!*server) message(server, server_cap, "Jellyfin");
    return 1;
}
int xf_auth_info(const xf_response *r, char *token, size_t token_cap, char *id, size_t id_cap, char *name, size_t name_cap) {
    xf_wipe(token, token_cap); xf_wipe(id, id_cap); xf_wipe(name, name_cap);
    if (!r->complete || r->failed || r->status != 200) return 0;
    if (xf_json_string(r->body, r->body_size, "AccessToken", token, token_cap) != 1 || !*token ||
        xf_json_string(r->body, r->body_size, "User.Id", id, id_cap) != 1 || !*id ||
        xf_json_string(r->body, r->body_size, "User.Name", name, name_cap) < 0) {
        xf_wipe(token, token_cap); xf_wipe(id, id_cap); xf_wipe(name, name_cap); return 0;
    }
    return 1;
}
static const char letters[] = "1234567890qwertyuiopasdfghjkl;zxcvbnm,./@:-_[]!?";
static const char symbols[] = "!@#$%^&*()_+-={}[]|\\:;\"'<>,.?/~`=";
void xf_keyboard_init(xf_keyboard *k, const char *initial, int secret) {
    xf_wipe(k, sizeof(*k)); k->secret = secret;
    snprintf(k->value, sizeof(k->value), "%s", initial ? initial : "");
}
size_t xf_keyboard_count(const xf_keyboard *k) { return strlen(k->symbols ? symbols : letters) + 4; }
const char *xf_keyboard_key(const xf_keyboard *k, size_t index, char label[8]) {
    const char *layout = k->symbols ? symbols : letters;
    static const char *extra[] = {"SPACE", "SHIFT", "DEL", "DONE"};
    size_t count = strlen(layout);
    if (index >= count) return index < count + 4 ? extra[index - count] : "";
    label[0] = k->shift ? (char)toupper((unsigned char)layout[index]) : layout[index]; label[1] = 0;
    return label;
}
int xf_keyboard_input(xf_keyboard *k, char action) {
    size_t count = xf_keyboard_count(k), size = strlen(k->value);
    if (action == 'L' && k->selected) --k->selected;
    if (action == 'R' && k->selected + 1 < count) ++k->selected;
    if (action == 'U' && k->selected >= 10) k->selected -= 10;
    if (action == 'D') k->selected = k->selected + 10 < count ? k->selected + 10 : count - 1;
    if (action == 'Y') { k->symbols = !k->symbols; k->selected = 0; }
    if (action == 'X' && size) k->value[size - 1] = 0;
    if (action == 'A') {
        char label[8]; const char *key = xf_keyboard_key(k, k->selected, label);
        if (!strcmp(key, "DONE")) return 1;
        if (!strcmp(key, "SHIFT")) k->shift = !k->shift;
        else if (!strcmp(key, "DEL")) { if (size) k->value[size - 1] = 0; }
        else if (size < XF_TEXT_LIMIT) { k->value[size] = !strcmp(key, "SPACE") ? ' ' : key[0]; k->value[size + 1] = 0; }
    }
    return 0;
}
