/* Host fault-injection shim for the real tcp_client.c, using the actual bundled
 * lwIP public structs/signatures. This does not simulate Xbox Ethernet/DHCP. */
#include "../native_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, closes, aborts, frees, writes;
static int write_oom, close_oom, connect_error;
static unsigned window=8192;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"native TCP line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
struct tcp_pcb *tcp_new(void){struct tcp_pcb *p=calloc(1,sizeof(*p));if(p)p->snd_buf=(u16_t)window;return p;}
void tcp_arg(struct tcp_pcb *p,void *a){p->callback_arg=a;}
void tcp_recv(struct tcp_pcb *p,tcp_recv_fn f){p->recv=f;}
void tcp_sent(struct tcp_pcb *p,tcp_sent_fn f){p->sent=f;}
void tcp_err(struct tcp_pcb *p,tcp_err_fn f){p->errf=f;}
void tcp_poll(struct tcp_pcb *p,tcp_poll_fn f,u8_t interval){p->poll=f;p->pollinterval=interval;}
err_t tcp_connect(struct tcp_pcb *p,ip_addr_t *ip,u16_t port,tcp_connected_fn f){(void)ip;CHECK(port==8096);p->connected=f;return (err_t)connect_error;}
err_t tcp_write(struct tcp_pcb *p,const void *data,u16_t size,u8_t flags){(void)data;CHECK(flags==TCP_WRITE_FLAG_COPY);if(write_oom)return ERR_MEM;CHECK(size<=p->snd_buf);p->snd_buf-=size;++writes;return ERR_OK;}
err_t tcp_output(struct tcp_pcb *p){(void)p;return ERR_OK;}
void tcp_recved(struct tcp_pcb *p,u16_t size){(void)p;(void)size;}
u8_t pbuf_free(struct pbuf *p){u8_t n=0;while(p){struct pbuf *next=p->next;free(p->payload);free(p);++n;++frees;p=next;}return n;}
err_t tcp_close(struct tcp_pcb *p){if(close_oom)return ERR_MEM;++closes;CHECK(!p->callback_arg&&!p->recv&&!p->sent&&!p->errf);free(p);return ERR_OK;}
void tcp_abort(struct tcp_pcb *p){++aborts;CHECK(!p->callback_arg&&!p->recv&&!p->sent&&!p->errf);free(p);}
static xf_http_client c;
static xf_endpoint ep;
static void start(uint32_t now,int auth){xf_http_init(&c);CHECK(xf_http_begin(&c,&ep,"device",auth?"user":NULL,auth?"test-secret":NULL,now));}
static void establish(void){CHECK(c.pcb->connected(c.pcb->callback_arg,c.pcb,ERR_OK)==ERR_OK);}
static void data(const char *s){struct pbuf *p=calloc(1,sizeof(*p));p->len=p->tot_len=(u16_t)strlen(s);p->payload=malloc(p->len);memcpy(p->payload,s,p->len);CHECK(c.pcb->recv(c.pcb->callback_arg,c.pcb,p,ERR_OK)==ERR_OK);}
int main(void){char err[128];size_t i;CHECK(xf_endpoint_parse("192.168.68.202:8096",&ep,err,sizeof(err)));
 start(0,0);establish();xf_http_tick(&c,1,1);data("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}");CHECK(c.done&&c.active);xf_http_tick(&c,2,1);CHECK(!c.active&&!c.pcb&&closes==1&&frees==1);
 start(0,1);establish();CHECK(strstr(c.request,"test-secret"));xf_http_cancel(&c);CHECK(!c.active&&c.done&&!c.request[0]&&aborts==1);
 start(0,0);xf_http_tick(&c,8000,1);CHECK(c.done&&!c.active&&c.error_code==ERR_TIMEOUT&&strstr(c.error,"connect"));
 start(0,0);establish();xf_http_tick(&c,1,1);xf_http_tick(&c,8001,1);CHECK(c.done&&!c.active&&strstr(c.error,"idle"));
 start(0,0);establish();xf_http_tick(&c,1,1);data("HTTP/1.1 200 OK\r\n");xf_http_tick(&c,7000,1);data("X: a\r\n");xf_http_tick(&c,14000,1);data("Y: b\r\n");xf_http_tick(&c,20000,1);CHECK(c.done&&!c.active&&strstr(c.error,"total"));
 start(UINT32_MAX-4000,0);xf_http_tick(&c,4000,1);CHECK(c.done&&c.error_code==ERR_TIMEOUT);
 start(0,0);xf_http_tick(&c,1,0);CHECK(c.done&&!c.active&&strstr(c.error,"DHCP"));
 start(0,0);establish();{tcp_err_fn f=c.pcb->errf;void *arg=c.pcb->callback_arg;free(c.pcb);f(arg,ERR_RST);}CHECK(!c.pcb&&c.done);xf_http_tick(&c,1,1);CHECK(!c.active&&c.error_code==ERR_RST);
 start(0,0);establish();data("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nabc");CHECK(c.pcb->recv(c.pcb->callback_arg,c.pcb,NULL,ERR_OK)==ERR_OK);xf_http_tick(&c,1,1);CHECK(!c.active&&c.response.failed);
 write_oom=1;start(0,0);establish();CHECK(!c.queued);write_oom=0;xf_http_tick(&c,1,1);CHECK(c.queued>0);xf_http_cancel(&c);
 window=16;start(0,1);establish();CHECK(c.queued==16);
 while(c.acknowledged<c.request_size){u16_t ack=(u16_t)(c.queued-c.acknowledged);CHECK(ack);c.pcb->snd_buf+=ack;CHECK(c.pcb->sent(c.pcb->callback_arg,c.pcb,ack)==ERR_OK);}
 for(i=0;i<sizeof(c.request);++i){CHECK(!c.request[i]);}
 xf_http_cancel(&c);window=8192;
 close_oom=1;start(0,0);establish();data("HTTP/1.1 204 No Content\r\n\r\n");xf_http_tick(&c,1,1);CHECK(!c.active&&!c.pcb);close_oom=0;
 connect_error=ERR_RTE;xf_http_init(&c);CHECK(!xf_http_begin(&c,&ep,"device",NULL,NULL,0)&&c.done&&!c.active&&c.error_code==ERR_RTE);connect_error=0;
 CHECK(writes>0);printf("Native raw TCP callbacks/timeouts/cancellation/cleanup: %u checks passed\n",checks);return 0;}
