/* Shared raw-lwIP fault shim. CHECK is supplied by each test suite. */
static unsigned closes, aborts, frees, writes;
static int write_oom, close_oom, connect_error;
static unsigned window=8192;
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
