#ifndef XF_TEST_POSIX_TCP_H
#define XF_TEST_POSIX_TCP_H
#include "../native_net.h"
void xf_test_tcp_init(const xf_endpoint *);
void xf_test_tcp_poll(void);
unsigned xf_test_tcp_backpressure(void);
void xf_test_tcp_finish(void);
#endif
