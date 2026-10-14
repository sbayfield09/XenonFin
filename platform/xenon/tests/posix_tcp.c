/* Test bridge only: actual host TCP carries native tcp_client.c's requests and
 * responses. Production uses the pinned real lwIP and NIC, never these stubs. */
#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
/* The pinned lwIP and POSIX headers use this name for unrelated options. */
#undef IP_OPTIONS
#include "posix_tcp.h"
/* The socket bridge calls libc, not lwIP's byte-order aliases. */
#undef htons
#undef htonl
#define SLOTS 20
typedef struct {
    struct tcp_pcb *pcb;
    int socket, connecting, eof;
    unsigned char outgoing[8192];
    size_t outgoing_size, acknowledged;
    struct pbuf *refused;
} connection;
static connection connections[SLOTS];
static struct sockaddr_in endpoint;
static unsigned pressure;
void xf_test_tcp_init(const xf_endpoint *ep) {
    memset(connections,0,sizeof(connections));memset(&endpoint,0,sizeof(endpoint));
    endpoint.sin_family=AF_INET;inet_pton(AF_INET,ep->host,&endpoint.sin_addr);pressure=0;
}
unsigned xf_test_tcp_backpressure(void) { return pressure; }
static connection *find(struct tcp_pcb *p) { unsigned i;for(i=0;i<SLOTS;++i)if(connections[i].pcb==p)return &connections[i];abort(); }
struct tcp_pcb *tcp_new(void) {
    unsigned i;for(i=0;i<SLOTS;++i)if(!connections[i].pcb){
        connection *c=&connections[i];memset(c,0,sizeof(*c));c->socket=-1;c->pcb=calloc(1,sizeof(*c->pcb));
        if(c->pcb)c->pcb->snd_buf=8192;
        return c->pcb;
    }return NULL;
}
void tcp_arg(struct tcp_pcb *p,void *a){p->callback_arg=a;}
void tcp_recv(struct tcp_pcb *p,tcp_recv_fn f){p->recv=f;}
void tcp_sent(struct tcp_pcb *p,tcp_sent_fn f){p->sent=f;}
void tcp_err(struct tcp_pcb *p,tcp_err_fn f){p->errf=f;}
void tcp_poll(struct tcp_pcb *p,tcp_poll_fn f,u8_t interval){p->poll=f;p->pollinterval=interval;}
err_t tcp_connect(struct tcp_pcb *p,ip_addr_t *ip,u16_t port,tcp_connected_fn callback){
    connection *c=find(p);struct sockaddr_in address=endpoint;int flags,rc;(void)ip;
    c->socket=socket(AF_INET,SOCK_STREAM,0);if(c->socket<0)return ERR_MEM;
    flags=fcntl(c->socket,F_GETFL,0);if(flags<0||fcntl(c->socket,F_SETFL,flags|O_NONBLOCK)<0)return ERR_CONN;
    address.sin_port=htons(port);p->connected=callback;
    rc=connect(c->socket,(struct sockaddr*)&address,sizeof(address));
    if(rc<0&&errno!=EINPROGRESS)return ERR_CONN;
    c->connecting=1;return ERR_OK;
}
err_t tcp_write(struct tcp_pcb *p,const void *bytes,u16_t size,u8_t flags){
    connection *c=find(p);(void)flags;
    if(size>p->snd_buf||size>sizeof(c->outgoing)-c->outgoing_size)return ERR_MEM;
    memcpy(c->outgoing+c->outgoing_size,bytes,size);c->outgoing_size+=size;p->snd_buf-=size;return ERR_OK;
}
err_t tcp_output(struct tcp_pcb *p){
    connection *c=find(p);ssize_t n;
    if(c->connecting||!c->outgoing_size)return ERR_OK;
    n=send(c->socket,c->outgoing,c->outgoing_size,MSG_NOSIGNAL);
    if(n<0)return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?ERR_MEM:ERR_RST;
    c->acknowledged+=(size_t)n;c->outgoing_size-=(size_t)n;memmove(c->outgoing,c->outgoing+n,c->outgoing_size);return ERR_OK;
}
void tcp_recved(struct tcp_pcb *p,u16_t n){(void)p;(void)n;}
u8_t pbuf_free(struct pbuf *p){u8_t n=0;while(p){struct pbuf *next=p->next;free(p->payload);free(p);p=next;++n;}return n;}
static void release(connection *c){
    if(c->socket>=0)close(c->socket);
    if(c->refused)pbuf_free(c->refused);
    free(c->pcb);memset(c,0,sizeof(*c));
}
err_t tcp_close(struct tcp_pcb *p){release(find(p));return ERR_OK;}
void tcp_abort(struct tcp_pcb *p){release(find(p));}
static void lost(connection *c,err_t error){tcp_err_fn fn=c->pcb->errf;void *arg=c->pcb->callback_arg;release(c);if(fn)fn(arg,error);}
void xf_test_tcp_poll(void){
    unsigned i;
    for(i=0;i<SLOTS;++i){
        connection *c=&connections[i];struct tcp_pcb *p=c->pcb;
        if(!p)continue;
        if(c->connecting){
            int error=0; socklen_t size=sizeof(error);struct sockaddr_in peer;socklen_t peer_size=sizeof(peer);
            if(getsockopt(c->socket,SOL_SOCKET,SO_ERROR,&error,&size)<0||error){lost(c,ERR_RST);continue;}
            if(getpeername(c->socket,(struct sockaddr*)&peer,&peer_size)<0)continue;
            c->connecting=0;if(p->connected)p->connected(p->callback_arg,p,ERR_OK);
        }
        tcp_output(p);
        if(c->acknowledged){u16_t n=(u16_t)c->acknowledged;c->acknowledged=0;p->snd_buf+=n;if(p->sent)p->sent(p->callback_arg,p,n);}
        if(!p->recv||c->eof)continue;
        if(c->refused){
            struct pbuf *packet=c->refused;c->refused=NULL;
            if(p->recv(p->callback_arg,p,packet,ERR_OK)==ERR_MEM){c->refused=packet;++pressure;continue;}
        }
        {
            unsigned char buffer[4096];ssize_t n=recv(c->socket,buffer,sizeof(buffer),0);
            if(n>0){
                struct pbuf *packet=calloc(1,sizeof(*packet));if(!packet)abort();packet->payload=malloc((size_t)n);if(!packet->payload)abort();
                memcpy(packet->payload,buffer,(size_t)n);packet->len=packet->tot_len=(u16_t)n;
                if(p->recv(p->callback_arg,p,packet,ERR_OK)==ERR_MEM){c->refused=packet;++pressure;}
            }else if(!n){c->eof=1;p->recv(p->callback_arg,p,NULL,ERR_OK);}
            else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)lost(c,ERR_RST);
        }
    }
}
void xf_test_tcp_finish(void){unsigned i;for(i=0;i<SLOTS;++i)if(connections[i].pcb)release(&connections[i]);}
