#ifndef XF_NATIVE_NET_H
#define XF_NATIVE_NET_H
#include "protocol.h"
#include <lwip/tcp.h>

typedef struct {
    struct tcp_pcb *pcb;
    xf_response response;
    char request[XF_REQUEST_LIMIT];
    size_t request_size, queued, acknowledged, received_size, observed;
    uint32_t began, progress;
    int active, connected, done, sensitive, error_code;
    char error[128];
    size_t (*receive_space)(void *);
    void *stream_context;
    int streaming, backpressure;
} xf_http_client;
void xf_http_init(xf_http_client *);
int xf_http_begin(xf_http_client *, const xf_endpoint *, const char *device,
                  const char *user, const char *password, uint32_t now);
void xf_http_tick(xf_http_client *, uint32_t now, int network_ready);
int xf_http_get(xf_http_client *, const xf_endpoint *, const char *device,
                const char *token, const char *resource, uint32_t now);
void xf_http_cancel(xf_http_client *);
int xf_http_api(xf_http_client *, const xf_endpoint *, const char *device, const char *token,
                const char *method, const char *resource, const char *json, uint32_t now);
int xf_http_video(xf_http_client *, const xf_endpoint *, const char *device, const char *token,
                  const char *resource, int (*sink)(void *, unsigned char), size_t (*space)(void *), void *, uint32_t now);
int xf_http_audio(xf_http_client *, const xf_endpoint *, const char *device, const char *token,
                  const char *resource, int (*sink)(void *, unsigned char), size_t (*space)(void *), void *, uint32_t now);

int xf_network_start(uint32_t now);
void xf_network_tick(uint32_t now);
int xf_network_ready(void);
int xf_network_available(void);
const char *xf_network_status(void);
const char *xf_dhcp_state(void);
#endif
