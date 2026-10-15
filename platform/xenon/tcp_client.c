/* lwIP raw callbacks all execute from network_poll on the UI thread. No sockets,
 * threads, busy waits, or blocking reads. Request buffers never reach printf. */
#include "native_net.h"
#include <stdio.h>
#include <string.h>

static const char *tcp_error(int err) {
    switch (err) {
    case ERR_MEM: return "lwIP memory/send buffer exhausted";
    case ERR_RTE: return "No IP route; check subnet/gateway";
    case ERR_RST: return "TCP reset/refused; check server port";
    case ERR_ABRT: return "TCP connection aborted";
    case ERR_CLSD: return "TCP connection closed";
    case ERR_CONN: return "TCP connection is not established";
    case ERR_TIMEOUT: return "TCP timed out";
    default: return "TCP transport error";
    }
}
/* Only called outside lwIP callbacks; safe to abort after callbacks have returned. */
static void detach(xf_http_client *c, int graceful) {
    struct tcp_pcb *pcb = c->pcb;
    c->pcb = NULL;
    if (pcb) {
        tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_sent(pcb, NULL); tcp_err(pcb, NULL); tcp_poll(pcb, NULL, 0);
        if (!graceful || tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    }
    xf_wipe(c->request, sizeof(c->request)); c->active = 0;
}
static void error(xf_http_client *c, int code, const char *text) {
    c->error_code = code; snprintf(c->error, sizeof(c->error), "%s", text); c->done = 1;
}
static void send_more(xf_http_client *c) {
    size_t amount;
    err_t rc;
    if (!c->pcb || !c->connected || c->done || c->queued == c->request_size) return;
    amount = c->request_size - c->queued;
    if (amount > tcp_sndbuf(c->pcb)) amount = tcp_sndbuf(c->pcb);
    if (amount > 1024) amount = 1024; /* Per-frame work remains bounded. */
    if (!amount) return;
    rc = tcp_write(c->pcb, c->request + c->queued, (u16_t)amount, TCP_WRITE_FLAG_COPY);
    if (rc == ERR_MEM) return; /* Retry next poll/ACK, bounded by idle/total deadlines. */
    if (rc != ERR_OK) { error(c, rc, tcp_error(rc)); return; }
    c->queued += amount;
    rc = tcp_output(c->pcb);
    if (rc != ERR_OK && rc != ERR_MEM) error(c, rc, tcp_error(rc));
}
static err_t connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    xf_http_client *c = (xf_http_client *)arg;
    (void)pcb;
    if (err != ERR_OK) error(c, err, tcp_error(err));
    else { c->connected = 1; send_more(c); }
    return ERR_OK;
}
static err_t received(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    xf_http_client *c = (xf_http_client *)arg;
    struct pbuf *part;
    if (p) {
        /* ERR_MEM transfers no ownership and acknowledges no bytes. lwIP retains
         * and retries this exact pbuf, including when playback is paused. */
        if (c->streaming && !c->done && err == ERR_OK && c->receive_space(c->stream_context) < p->tot_len) {
            c->backpressure = 1; return ERR_MEM;
        }
        c->backpressure = 0;
        c->received_size += p->tot_len;
        if (err != ERR_OK) error(c, err, tcp_error(err));
        else {
            for (part = p; part; part = part->next) xf_response_feed(&c->response, part->payload, part->len);
        }
        tcp_recved(pcb, p->tot_len); pbuf_free(p);
    } else if (err != ERR_OK) error(c, err, tcp_error(err));
    else xf_response_eof(&c->response);
    if (c->response.failed) error(c, 0, c->response.error);
    if (c->response.complete) c->done = 1;
    /* Do not abort/close/free a PCB from this callback. Tick owns cleanup. */
    return ERR_OK;
}
static err_t sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    xf_http_client *c = (xf_http_client *)arg;
    (void)pcb; c->acknowledged += len;
    if (c->acknowledged >= c->request_size) xf_wipe(c->request, sizeof(c->request));
    send_more(c); return ERR_OK;
}
static void lost(void *arg, err_t err) {
    xf_http_client *c = (xf_http_client *)arg;
    /* lwIP already freed this PCB. Never close or dereference it again. */
    c->pcb = NULL;
    if (!c->response.complete) error(c, err, tcp_error(err));
    else c->done = 1;
}
void xf_http_init(xf_http_client *c) { memset(c, 0, sizeof(*c)); xf_response_init(&c->response); }
void xf_http_cancel(xf_http_client *c) {
    if (!c->active) return;
    error(c, 0, "Request cancelled"); detach(c, 0);
    if (c->sensitive) xf_wipe(c->response.body, sizeof(c->response.body));
}
static int begin_transport(xf_http_client *c, const xf_endpoint *ep, int count, uint32_t now) {
    ip_addr_t address;
    err_t rc;
    if (count < 0) { error(c, 0, "Invalid or oversized HTTP request"); return 0; }
    c->request_size = (size_t)count; c->began = c->progress = now;
    c->pcb = tcp_new();
    if (!c->pcb) { error(c, ERR_MEM, "No free TCP connection"); xf_wipe(c->request, sizeof(c->request)); return 0; }
    c->active = 1;
    tcp_arg(c->pcb, c); tcp_recv(c->pcb, received); tcp_sent(c->pcb, sent); tcp_err(c->pcb, lost);
    IP4_ADDR(&address, ep->octets[0], ep->octets[1], ep->octets[2], ep->octets[3]);
    rc = tcp_connect(c->pcb, &address, ep->port, connected);
    if (rc != ERR_OK) { error(c, rc, tcp_error(rc)); detach(c, 0); return 0; }
    return 1;
}
int xf_http_begin(xf_http_client *c, const xf_endpoint *ep, const char *device,
                  const char *user, const char *password, uint32_t now) {
    int count;
    xf_http_cancel(c); xf_http_init(c); c->sensitive = user != NULL;
    count = xf_request(c->request, sizeof(c->request), ep, device, user, password);
    return begin_transport(c, ep, count, now);
}
int xf_http_get(xf_http_client *c, const xf_endpoint *ep, const char *device,
                const char *token, const char *resource, uint32_t now) {
    int count;
    xf_http_cancel(c); xf_http_init(c); c->sensitive = 1;
    count = xf_get_request(c->request, sizeof(c->request), ep, device, token, resource);
    return begin_transport(c, ep, count, now);
}
int xf_http_api(xf_http_client *c, const xf_endpoint *ep, const char *device, const char *token,
                const char *method, const char *resource, const char *json, uint32_t now) {
    int count;
    xf_http_cancel(c); xf_http_init(c); c->sensitive = 1;
    count = xf_api_request(c->request, sizeof(c->request), ep, device, token, method, resource, json, 0);
    return begin_transport(c, ep, count, now);
}
int xf_http_audio(xf_http_client *c, const xf_endpoint *ep, const char *device, const char *token,
                  const char *resource, int (*sink)(void *, unsigned char), size_t (*space)(void *), void *context, uint32_t now) {
    int count;
    xf_http_cancel(c); xf_http_init(c); c->sensitive = 1; c->streaming = 1;
    if (!sink || !space) { error(c, 0, "Missing audio receiver"); return 0; }
    c->receive_space = space; c->stream_context = context;
    xf_response_stream(&c->response, sink, context, 512U * 1024U * 1024U);
    count = xf_api_request(c->request, sizeof(c->request), ep, device, token, "GET", resource, NULL, 1);
    return begin_transport(c, ep, count, now);
}
int xf_http_video(xf_http_client *c,const xf_endpoint *ep,const char *device,const char *token,
                  const char *resource,int (*sink)(void *,unsigned char),size_t (*space)(void *),void *context,uint32_t now) {
    int count;xf_http_cancel(c);xf_http_init(c);c->sensitive=1;c->streaming=1;
    if(!sink || !space) {error(c,0,"Missing video receiver");return 0;}
    c->receive_space=space;c->stream_context=context;
    xf_response_stream(&c->response,sink,context,UINT64_MAX);
    count=xf_api_request(c->request,sizeof(c->request),ep,device,token,"GET",resource,NULL,2);
    return begin_transport(c,ep,count,now);
}
void xf_http_tick(xf_http_client *c, uint32_t now, int ready) {
    if (!c->active) return;
    /* Tick observes receive/ACK activity without depending on a hardware clock in callbacks. */
    size_t progress = c->received_size + c->acknowledged + (size_t)c->connected;
    if (progress != c->observed) { c->observed = progress; c->progress = now; }
    if (!c->done && !ready) error(c, 0, "DHCP lease/link unavailable; reconnect network");
    if (c->streaming && c->connected && c->backpressure && c->receive_space(c->stream_context) < TCP_WND) c->progress = now;
    if (!c->done && (!c->streaming || !c->response.headers_done) && (uint32_t)(now - c->began) >= 20000) error(c, ERR_TIMEOUT, "HTTP total timeout (20 seconds)");
    if (!c->done && !c->connected && (uint32_t)(now - c->began) >= 8000) error(c, ERR_TIMEOUT, "TCP connect timeout (8 seconds)");
    if (!c->done && c->connected && (uint32_t)(now - c->progress) >= 8000) error(c, ERR_TIMEOUT, "HTTP idle timeout (8 seconds)");
    if (c->done) { detach(c, !c->error[0]); return; }
    send_more(c);
    /* tcp_output can return ERR_MEM without sending an already-queued request.
     * Retry output even when there are no more application bytes to enqueue. */
    if (!c->done && c->connected && c->queued > c->acknowledged) {
        err_t rc = tcp_output(c->pcb);
        if (rc != ERR_OK && rc != ERR_MEM) error(c, rc, tcp_error(rc));
    }
    if (c->done) detach(c, 0);
}
