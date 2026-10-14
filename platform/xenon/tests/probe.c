/* Host-only integration harness for the exact C request/HTTP/JSON code linked
 * into the ELF. POSIX supplies TCP here; real Xenon Ethernet still needs testing. */
#define _POSIX_C_SOURCE 200809L
#include "../protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
static xf_response response;
static uint64_t now_ms(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;}
static int perform(const xf_endpoint *ep,const char *user,const char *password){
 char request[XF_REQUEST_LIMIT],buffer[1024];struct sockaddr_in address;struct pollfd poller;
 int sock=-1,n,flags,ok=0;size_t sent=0;uint64_t began=now_ms();
 n=xf_request(request,sizeof(request),ep,"xenonfin-native-host-test",user,password);if(n<0)return 0;
 xf_response_init(&response);sock=socket(AF_INET,SOCK_STREAM,0);if(sock<0)goto done;
 flags=fcntl(sock,F_GETFL,0);if(flags<0||fcntl(sock,F_SETFL,flags|O_NONBLOCK)<0)goto done;
 memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_port=htons(ep->port);
 if(inet_pton(AF_INET,ep->host,&address.sin_addr)!=1)goto done;
 if(connect(sock,(struct sockaddr*)&address,sizeof(address))<0&&errno!=EINPROGRESS)goto done;
 poller.fd=sock;poller.events=POLLOUT;
 while(now_ms()-began<20000){
  int result=poll(&poller,1,100);if(result<0){if(errno==EINTR)continue;goto done;}if(!result)continue;
  if(poller.events==POLLOUT){
   int error=0;socklen_t size=sizeof(error);if(getsockopt(sock,SOL_SOCKET,SO_ERROR,&error,&size)<0||error)goto done;
   while(sent<(size_t)n){ssize_t count=send(sock,request+sent,(size_t)n-sent,MSG_NOSIGNAL);if(count>0)sent+=(size_t)count;else if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;else goto done;}
   if(sent==(size_t)n){xf_wipe(request,sizeof(request));poller.events=POLLIN;}
  }else{
   ssize_t count=recv(sock,buffer,sizeof(buffer),0);
   if(count>0)xf_response_feed(&response,buffer,(size_t)count);
   else if(!count)xf_response_eof(&response);
   else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)goto done;
   if(response.failed)goto done;
   if(response.complete){ok=1;goto done;}
  }
 }
 done:xf_wipe(request,sizeof(request));xf_wipe(buffer,sizeof(buffer));if(sock>=0)close(sock);return ok;
}
int main(int argc,char **argv){
 xf_endpoint ep;char error[128],name[129],version[65],token[2049],id[129];
 const char *user=getenv("JELLYFIN_USERNAME"),*password=getenv("JELLYFIN_PASSWORD");
 int expected=argc==3?atoi(argv[2]):200;
 if(argc<2||argc>3||!xf_endpoint_parse(argv[1],&ep,error,sizeof(error))){fputs("Pass numeric HTTP IPv4[:port]\n",stderr);return 1;}
 if(!perform(&ep,NULL,NULL)||!xf_public_info(&response,name,sizeof(name),version,sizeof(version))){fprintf(stderr,"Native public-info protocol failed (HTTP %d)\n",response.status);return 1;}
 printf("Native C public-info HTTP %d, valid Jellyfin JSON\n",response.status);
 if(user&&password){
  if(!perform(&ep,user,password)){fputs("Native authentication transport failed\n",stderr);return 1;}
  if(response.status!=expected){fprintf(stderr,"Unexpected native authentication HTTP %d\n",response.status);return 1;}
  if(expected==200&&!xf_auth_info(&response,token,sizeof(token),id,sizeof(id),name,sizeof(name))){fputs("Native authentication JSON invalid\n",stderr);return 1;}
  printf("Native C authentication HTTP %d, %s\n",response.status,expected==200?"token/user parsed (redacted)":"expected rejection; body hidden");
  xf_wipe(token,sizeof(token));xf_wipe(id,sizeof(id));xf_wipe(&response,sizeof(response));
 }
 return 0;
}
