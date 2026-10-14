/* Host-only integration harness for the exact C request/HTTP/JSON code linked
 * into the ELF. POSIX supplies TCP here; real Xenon Ethernet still needs testing. */
#define _POSIX_C_SOURCE 200809L
#include "../protocol.h"
#include "../browse.h"
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
static const char *get_path, *get_token;
static xf_browser browser;
static uint64_t now_ms(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;}
static int perform(const xf_endpoint *ep,const char *user,const char *password){
 char request[XF_REQUEST_LIMIT],buffer[1024];struct sockaddr_in address;struct pollfd poller;
 int sock=-1,n,flags,ok=0;size_t sent=0;uint64_t began=now_ms();
 n=get_path?xf_get_request(request,sizeof(request),ep,"xenonfin-native-host-test",get_token,get_path):xf_request(request,sizeof(request),ep,"xenonfin-native-host-test",user,password);if(n<0)return 0;
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
static int browse_action(const xf_endpoint *ep, const char *id, const char *token, enum xf_browse_action action) {
 char path[1501]; int rc=xf_browser_request(&browser,action,id,path,sizeof(path));
 if(rc==0)return 1;
 if(rc!=1)return 0;
 get_path=path;get_token=token;rc=perform(ep,NULL,NULL);get_path=get_token=NULL;
 if(!rc){fprintf(stderr,"Native browse transport HTTP %d: %s\n",response.status,response.error);return 0;}
 rc=xf_browser_result(&browser,&response,"");xf_wipe(&response,sizeof(response));
 if(rc!=1){fprintf(stderr,"Native browser result: %s\n",browser.message);return 0;}
 return 1;
}
#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"Real native browsing check line %d failed: %s\n",__LINE__,#x);return 0;} } while(0)
static int select_type(const char *type) {
 unsigned i;for(i=0;i<browser.page.count;++i)if(!strcmp(browser.page.items[i].type,type)){browser.current.selected=i;return 1;}return 0;
}
static int select_library(const char *name) {
 unsigned i;for(i=0;i<browser.page.count;++i)if(!strcmp(browser.page.items[i].name,name)){browser.current.selected=i;return 1;}return 0;
}
static int browse_integration(const xf_endpoint *ep,const char *id,const char *token,int restricted){
 char path[1501];int rc;
 xf_browser_init(&browser);
 REQUIRE(browse_action(ep,id,token,XF_ROOT));
 REQUIRE(browser.page.count==(restricted?1:5));
 REQUIRE(select_library("Native Movies"));REQUIRE(browse_action(ep,id,token,XF_OPEN));
 REQUIRE(browser.page.count==10&&browser.page.total==12);
 REQUIRE(browse_action(ep,id,token,XF_NEXT)&&browser.page.count==2&&browser.current.start==10);
 REQUIRE(browse_action(ep,id,token,XF_PREVIOUS)&&browser.page.count==10&&browser.current.start==0);
 REQUIRE(select_type("Movie")&&browse_action(ep,id,token,XF_OPEN)&&browser.details);
 REQUIRE(browser.detail.year==2024&&strstr(browser.detail.overview,"Native movie synopsis"));
 REQUIRE(browse_action(ep,id,token,XF_BACK)&&!browser.details);
 REQUIRE(browse_action(ep,id,token,XF_BACK)&&!browser.depth);
 if(!restricted){
  REQUIRE(select_library("Native TV")&&browse_action(ep,id,token,XF_OPEN));
  REQUIRE(select_type("Series")&&browse_action(ep,id,token,XF_DETAILS)&&browser.details);
  REQUIRE(!strcmp(browser.detail.type,"Series"));REQUIRE(browse_action(ep,id,token,XF_BACK));
  REQUIRE(browse_action(ep,id,token,XF_OPEN)&&select_type("Season"));
  REQUIRE(browse_action(ep,id,token,XF_DETAILS)&&!strcmp(browser.detail.type,"Season"));REQUIRE(browse_action(ep,id,token,XF_BACK));
  REQUIRE(browse_action(ep,id,token,XF_OPEN)&&select_type("Episode"));
  REQUIRE(browse_action(ep,id,token,XF_OPEN)&&browser.details&&!strcmp(browser.detail.type,"Episode"));
  REQUIRE(browser.detail.index==1&&browser.detail.season==1&&browser.detail.series[0]);
  REQUIRE(browse_action(ep,id,token,XF_ROOT)&&select_library("Native Music"));
  REQUIRE(browse_action(ep,id,token,XF_OPEN)&&select_type("MusicArtist"));
  REQUIRE(browse_action(ep,id,token,XF_DETAILS)&&browser.details&&!strcmp(browser.detail.type,"MusicArtist"));
  REQUIRE(browse_action(ep,id,token,XF_BACK)&&browse_action(ep,id,token,XF_OPEN)&&select_type("MusicAlbum"));
  REQUIRE(browse_action(ep,id,token,XF_DETAILS)&&browser.details&&!strcmp(browser.detail.type,"MusicAlbum"));
  REQUIRE(browse_action(ep,id,token,XF_BACK)&&browse_action(ep,id,token,XF_OPEN)&&select_type("Audio"));
  REQUIRE(browser.page.count==2&&browse_action(ep,id,token,XF_OPEN)&&browser.details);
  REQUIRE(browser.detail.artist[0]&&browser.detail.album[0]&&browser.detail.ticks);
  REQUIRE(browse_action(ep,id,token,XF_BACK)&&browse_action(ep,id,token,XF_BACK)&&browse_action(ep,id,token,XF_BACK));
  REQUIRE(browse_action(ep,id,token,XF_MUSIC)&&select_type("MusicAlbum"));
  REQUIRE(browse_action(ep,id,token,XF_MUSIC)&&select_type("Audio"));
  REQUIRE(browse_action(ep,id,token,XF_ROOT)&&select_library("Native Empty"));
  REQUIRE(browse_action(ep,id,token,XF_OPEN)&&!browser.page.count&&!browser.page.total);
  REQUIRE(browse_action(ep,id,token,XF_ROOT)&&select_library("Native Other"));
  REQUIRE(browse_action(ep,id,token,XF_OPEN)&&select_type("Book"));
  REQUIRE(browse_action(ep,id,token,XF_OPEN)&&browser.details&&!strcmp(browser.detail.type,"Book"));
 }
 REQUIRE(xf_browser_request(&browser,XF_ROOT,id,path,sizeof(path))==1);
 get_path=path;get_token="invalid-expired-native-test-token";rc=perform(ep,NULL,NULL);get_path=get_token=NULL;
 REQUIRE(rc&&response.status==401&&xf_browser_result(&browser,&response,"")==-2);
 xf_browser_init(&browser);xf_wipe(&response,sizeof(response));
 puts(restricted?"Native C restricted-user views, movie pagination/details and expired-session rejection PASS":"Native C real libraries, movie pagination, series/seasons/episodes, artists/albums/tracks, metadata, empty/other libraries and expired-session rejection PASS");
 return 1;
}
int main(int argc,char **argv){
 xf_endpoint ep;char error[128],name[129],version[65],token[2049],id[129];
 const char *user=getenv("JELLYFIN_USERNAME"),*password=getenv("JELLYFIN_PASSWORD");
 int browse=argc==3&&!strcmp(argv[2],"--browse"),restricted=argc==3&&!strcmp(argv[2],"--restricted");
 int expected=argc==3&&!browse&&!restricted?atoi(argv[2]):200;
 if(argc<2||argc>3||!xf_endpoint_parse(argv[1],&ep,error,sizeof(error))){fputs("Pass numeric HTTP IPv4[:port]\n",stderr);return 1;}
 if(!perform(&ep,NULL,NULL)||!xf_public_info(&response,name,sizeof(name),version,sizeof(version))){fprintf(stderr,"Native public-info protocol failed (HTTP %d)\n",response.status);return 1;}
 printf("Native C public-info HTTP %d, valid Jellyfin JSON\n",response.status);
 if(user&&password){
  if(!perform(&ep,user,password)){fputs("Native authentication transport failed\n",stderr);return 1;}
  if(response.status!=expected){fprintf(stderr,"Unexpected native authentication HTTP %d\n",response.status);return 1;}
  if(expected==200&&!xf_auth_info(&response,token,sizeof(token),id,sizeof(id),name,sizeof(name))){fputs("Native authentication JSON invalid\n",stderr);return 1;}
  printf("Native C authentication HTTP %d, %s\n",response.status,expected==200?"token/user parsed (redacted)":"expected rejection; body hidden");
  if((browse||restricted)&&!browse_integration(&ep,id,token,restricted)){xf_wipe(token,sizeof(token));return 1;}
  xf_wipe(token,sizeof(token));xf_wipe(id,sizeof(id));xf_wipe(&response,sizeof(response));
 }
 return 0;
}
