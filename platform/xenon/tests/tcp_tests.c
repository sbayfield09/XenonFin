/* Host fault-injection shim for the real tcp_client.c, using the actual bundled
 * lwIP public structs/signatures. This does not simulate Xbox Ethernet/DHCP. */
#include "../native_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"native TCP line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
#include "tcp_fake.h"
static xf_http_client c;
static xf_endpoint ep;
static size_t stream_space = 8192, streamed;
static size_t space(void *p){(void)p;return stream_space;}
static int sink(void *p,unsigned char byte){(void)p;(void)byte;++streamed;return 1;}
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
 xf_http_init(&c);CHECK(xf_http_get(&c,&ep,"device","browse-token","/Items?Limit=10",0));CHECK(c.sensitive&&strstr(c.request,"Token=\"browse-token\""));establish();data("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}");xf_http_tick(&c,1,1);CHECK(c.done&&!c.active&&!c.request[0]);
 CHECK(xf_http_get(&c,&ep,"device","browse-token","/Items",0));establish();data("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\npartial");xf_http_cancel(&c);CHECK(!c.request[0]&&!c.response.body[0]&&!c.active);
 CHECK(xf_http_audio(&c,&ep,"device","stream-token","/Audio/id/stream.mp3",sink,space,NULL,0));establish();
 {
  const char *wire="HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nmp3";unsigned freed=frees;struct pbuf *p=calloc(1,sizeof(*p));
  p->len=p->tot_len=(u16_t)strlen(wire);p->payload=malloc(p->len);memcpy(p->payload,wire,p->len);stream_space=0;
  CHECK(c.pcb->recv(c.pcb->callback_arg,c.pcb,p,ERR_OK)==ERR_MEM);CHECK(frees==freed&&!c.received_size&&!c.response.headers_done&&!streamed&&c.blocked_bytes==p->tot_len);
  stream_space=8192;CHECK(c.pcb->recv(c.pcb->callback_arg,c.pcb,p,ERR_OK)==ERR_OK);CHECK(frees==freed+1&&streamed==3&&c.response.body_size==3&&c.done&&c.window_recovery&&!c.backpressure&&!c.blocked_bytes);
 }
 xf_http_tick(&c,1,1);CHECK(!c.active&&!c.request[0]);
 CHECK(xf_http_audio(&c,&ep,"device","stream-token","/Audio/id/stream.mp3",sink,space,NULL,0));establish();data("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n");
 c.backpressure=1;c.blocked_bytes=TCP_WND+1000;stream_space=TCP_WND;xf_http_tick(&c,1,1);xf_http_tick(&c,60000,1);CHECK(c.active&&!c.done); /* Protect exact retained pbuf, not a guessed TCP_WND capacity. */
 stream_space=8192;xf_http_tick(&c,68000,1);CHECK(c.done&&!c.active&&strstr(c.error,"idle"));
 /* A reopened window whose first ACK was lost must be re-advertised. An ACK
  * attempt is not received data and must never postpone a real idle timeout. */
 CHECK(xf_http_video(&c,&ep,"device","stream-token","/Videos/id/stream.mpg",sink,space,NULL,0));establish();
 data("HTTP/1.1 200 OK\r\nContent-Length: 99999\r\n\r\n");xf_http_tick(&c,1,1);
 c.window_recovery=1;c.pcb->rcv_wnd=TCP_WND;c.pcb->rcv_ann_wnd=TCP_WND;
 xf_http_tick(&c,250,1);CHECK(!c.window_updates);
 xf_http_tick(&c,251,1);CHECK(c.window_updates==1&&(c.pcb->flags&TF_ACK_NOW));
 xf_http_tick(&c,300,1);CHECK(c.window_updates==1&&c.progress==1);
 data("x");xf_http_tick(&c,301,1);CHECK(!c.window_recovery&&c.progress==301);
 c.window_recovery=1;xf_http_tick(&c,8301,1);CHECK(c.done&&!c.active&&strstr(c.error,"idle")&&c.receive_window==TCP_WND&&c.idle_age==8000);
 CHECK(writes>0);printf("Native raw TCP callbacks/timeouts/cancellation/cleanup: %u checks passed\n",checks);return 0;}
