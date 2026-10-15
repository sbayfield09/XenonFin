#ifndef XF_NATIVE_PROTOCOL_H
#define XF_NATIVE_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define XF_BODY_LIMIT 32768
#define XF_HEADER_LIMIT 8192
#define XF_REQUEST_LIMIT 4096
#define XF_TEXT_LIMIT 128

typedef struct {
    uint8_t octets[4];
    uint16_t port;
    char host[16];
} xf_endpoint;

/* Native target intentionally supports numeric IPv4 HTTP only. */
int xf_endpoint_parse(const char *, xf_endpoint *, char *error, size_t);
void xf_wipe(void *, size_t);
/* 1: found string, 0: absent, -1: invalid JSON/type/overflow. Validates entire document. */
int xf_json_string(const char *, size_t, const char *path, char *, size_t);
typedef struct { const char *data; size_t size; } xf_json_ref;
/* Validated, borrowed spans. No allocation; refs expire with their input. */
int xf_json_get(xf_json_ref, const char *path, xf_json_ref *);
int xf_json_elements(xf_json_ref, size_t start, xf_json_ref *, size_t cap, size_t *count);
int xf_json_text(xf_json_ref, char *, size_t); /* Truncates at a UTF-8 boundary. */
int xf_json_uint(xf_json_ref, uint64_t *);
int xf_json_bool(xf_json_ref, int *);
int xf_json_quote(const char *, char *, size_t);
int xf_request(char *, size_t, const xf_endpoint *, const char *device_id,
               const char *username, const char *password);
int xf_get_request(char *, size_t, const xf_endpoint *, const char *device_id,
                   const char *token, const char *resource);
int xf_api_request(char *, size_t, const xf_endpoint *, const char *device_id,
                   const char *token, const char *method, const char *resource,
                   const char *json_body, int audio);

typedef struct {
    char header[XF_HEADER_LIMIT + 1];
    char body[XF_BODY_LIMIT + 1];
    char line[256];
    char error[128];
    size_t header_size, body_size, line_size, trailer_size;
    uint64_t remaining, streamed;
    int status, headers_done, complete, failed, chunked, has_length, phase;
    uint64_t body_limit;
    int (*sink)(void *, unsigned char);
    void *sink_context;
    char content_type[80];
} xf_response;

void xf_response_init(xf_response *);
void xf_response_stream(xf_response *, int (*sink)(void *, unsigned char), void *, uint64_t limit);
void xf_response_feed(xf_response *, const void *, size_t);
void xf_response_eof(xf_response *);
int xf_public_info(const xf_response *, char *server, size_t, char *version, size_t);
int xf_auth_info(const xf_response *, char *token, size_t, char *user_id, size_t, char *name, size_t);
/* Addresses are compared in their existing network-byte-order representation. */
int xf_lease_valid(int managed, int state, int up, uint32_t address,
                   uint32_t offered, uint32_t lease_seconds);

typedef struct {
    char value[XF_TEXT_LIMIT + 1];
    size_t selected;
    int secret, symbols, shift;
} xf_keyboard;
void xf_keyboard_init(xf_keyboard *, const char *, int secret);
size_t xf_keyboard_count(const xf_keyboard *);
const char *xf_keyboard_key(const xf_keyboard *, size_t, char label[8]);
/* Arrows: U/D/L/R; A: type, X: delete, Y: symbols. Returns 1 for DONE. */
int xf_keyboard_input(xf_keyboard *, char action);
#endif
