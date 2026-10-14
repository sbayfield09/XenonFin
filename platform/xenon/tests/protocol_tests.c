#include "../protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "native protocol: line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static xf_response response;
static void feed(const char *s, size_t fragment) {
    size_t i, length = strlen(s);
    xf_response_init(&response);
    for (i = 0; i < length; i += fragment) xf_response_feed(&response, s + i, length - i < fragment ? length - i : fragment);
}
static void json_tests(void) {
    char out[129];
    const char *doc = "{\"AccessToken\":\"fixture-token\",\"User\":{\"Id\":\"user\",\"Name\":\"A \\u2603 \\ud83d\\ude00\"},\"Other\":[true,false,null,-0.25e+3]}";
    CHECK(xf_json_string(doc, strlen(doc), "AccessToken", out, sizeof(out)) == 1 && !strcmp(out, "fixture-token"));
    CHECK(xf_json_string(doc, strlen(doc), "User.Id", out, sizeof(out)) == 1 && !strcmp(out, "user"));
    CHECK(xf_json_string(doc, strlen(doc), "User.Name", out, sizeof(out)) == 1 && !strcmp(out, "A \xe2\x98\x83 \xf0\x9f\x98\x80"));
    CHECK(xf_json_string(doc, strlen(doc), "missing", out, sizeof(out)) == 0 && !*out);
    {
        const char *bad[] = {"{}junk", "{\"x\":1,}", "{\"x\":[1,]}", "{\"x\":01}", "{\"x\":-}", "{\"x\":1.}", "{\"x\":1e}", "{\"x\":tru}",
            "{\"x\":\"\\ud800\"}", "{\"x\":\"\\udc00\"}", "{\"x\":\"\\ud800\\u0001\"}", "{\"x\":\"\xc0\xaf\"}", "{\"x\":\"\xed\xa0\x80\"}",
            "{\"x\":\"\xf4\x90\x80\x80\"}", "{\"x\":\"\xe2\x28\xa1\"}", "{\"x\":\"bad\n\"}", "[]", "{\"x\":\"a\",\"x\":\"b\"}", "{\"x\":false}"};
        size_t i;
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) CHECK(xf_json_string(bad[i], strlen(bad[i]), "x", out, sizeof(out)) == -1 && !*out);
    }
    CHECK(xf_json_string("{\"x\":\"abc\"}", 11, "x", out, 3) == -1);
    CHECK(xf_json_quote("a\"b\\c\n", out, sizeof(out)) && !strcmp(out, "\"a\\\"b\\\\c\\u000a\""));
    CHECK(!xf_json_quote("long", out, 3));
    { char nested[256]; int i; strcpy(nested, "{\"a\":"); for(i=0;i<30;++i)strcat(nested,"[");strcat(nested,"0");for(i=0;i<30;++i)strcat(nested,"]");strcat(nested,"}");CHECK(xf_json_string(nested,strlen(nested),"missing",out,sizeof(out))==-1); }
}
int main(void) {
    char error[128], request[XF_REQUEST_LIMIT], server[129], version[65], token[129], id[65], name[129];
    xf_endpoint ep;
    size_t i;
    CHECK(xf_endpoint_parse("192.168.68.202", &ep, error, sizeof(error)) && ep.port == 8096);
    CHECK(xf_endpoint_parse("http://192.168.68.202:8096/", &ep, error, sizeof(error)) && !strcmp(ep.host,"192.168.68.202"));
    CHECK(xf_endpoint_parse("127.0.0.1:65535", &ep, error, sizeof(error)) && ep.port == 65535);
    { const char *bad[]={"", "https://192.168.1.1", "host.local:8096", "192.168.1", "256.1.2.3", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4:123456", "1.2.3.4/a", "1.2.3.4?x", "1.2.3.4:80\r\nInjected:x", "0.0.0.0", "255.255.255.255", "224.0.0.1"};
      for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i)CHECK(!xf_endpoint_parse(bad[i],&ep,error,sizeof(error))); }
    CHECK(xf_endpoint_parse("192.168.68.202", &ep, error, sizeof(error)));
    CHECK(xf_request(request,sizeof(request),&ep,"xenonfin-test",NULL,NULL)>0 && strstr(request,"GET /System/Info/Public HTTP/1.1\r\n"));
    CHECK(strstr(request,"Host: 192.168.68.202:8096\r\n") && strstr(request,"Connection: close\r\n\r\n"));
    CHECK(xf_request(request,sizeof(request),&ep,"xenonfin-test","a\"b","s\r\necret")>0);
    CHECK(strstr(request,"POST /Users/AuthenticateByName") && strstr(request,"\"Pw\":\"s\\u000d\\u000aecret\"") && !strstr(request,"s\r\necret"));
    { char *body=strstr(request,"\r\n\r\n");char *length=strstr(request,"Content-Length: ");CHECK(body&&length&&strtoul(length+16,NULL,10)==strlen(body+4)); }
    CHECK(xf_request(request,32,&ep,"device","user","secret")==-1 && !request[0]);
    CHECK(xf_request(request,sizeof(request),&ep,"bad\r\nDevice",NULL,NULL)==-1);
    json_tests();
    for(i=1;i<30;++i) {
        feed("HTTP/1.1 200 OK\r\nContent-Length: 42\r\n\r\n{\"ServerName\":\"Example\",\"Version\":\"10.10\"}",i);
        CHECK(response.complete && !response.failed && response.status==200);
        CHECK(xf_public_info(&response,server,sizeof(server),version,sizeof(version)) && !strcmp(server,"Example"));
        feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;extension=yes\r\nabc\r\n2\r\nde\r\n0\r\nX-Trailer: yes\r\n\r\n",i);
        CHECK(response.complete && !response.failed && !strcmp(response.body,"abcde"));
    }
    feed("HTTP/1.0 404 Not Found\r\n\r\nnot found",1);CHECK(!response.complete);xf_response_eof(&response);CHECK(response.complete && response.status==404);
    feed("HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nshort",2);xf_response_eof(&response);CHECK(response.failed);
    { const char *bad[]={"HTTP/1.1 200 OK\r\nContent-Length: 9999999999999999\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\nab",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n",
        "HTTP/1.1 abc no\r\n\r\n", "HTTP/1.1 200 OK\r\nBad Header: x\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nz\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naXX"};
      for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i){feed(bad[i],1);CHECK(response.failed);} }
    feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\na",1);xf_response_eof(&response);CHECK(response.failed);
    feed("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n",2);CHECK(response.complete && !response.body_size);
    { char *large=malloc(XF_BODY_LIMIT+100);CHECK(large);memset(large,'a',XF_BODY_LIMIT+99);large[XF_BODY_LIMIT+99]=0;
      feed("HTTP/1.1 200 OK\r\n\r\n",1);xf_response_feed(&response,large,XF_BODY_LIMIT+99);CHECK(response.failed&&response.body_size==XF_BODY_LIMIT);
      xf_response_init(&response);xf_response_feed(&response,large,XF_HEADER_LIMIT+1);CHECK(response.failed);free(large); }
    feed("HTTP/1.1 200 OK\r\n\r\n{\"AccessToken\":\"fixture\",\"User\":{\"Id\":\"u\",\"Name\":\"user\"}}",1);xf_response_eof(&response);
    CHECK(xf_auth_info(&response,token,sizeof(token),id,sizeof(id),name,sizeof(name))&&!strcmp(token,"fixture")&&!strcmp(id,"u"));
    response.status=401;CHECK(!xf_auth_info(&response,token,sizeof(token),id,sizeof(id),name,sizeof(name))&&!token[0]&&!id[0]);
    CHECK(xf_lease_valid(1,10,1,123,123,3600));CHECK(xf_lease_valid(1,5,1,123,123,3600));CHECK(xf_lease_valid(1,4,1,123,123,3600));
    CHECK(!xf_lease_valid(0,10,1,123,123,3600));CHECK(!xf_lease_valid(1,6,1,123,123,3600));CHECK(!xf_lease_valid(1,10,1,123,124,3600));
    CHECK(!xf_lease_valid(1,10,0,123,123,3600));CHECK(!xf_lease_valid(1,10,1,123,123,0));CHECK(!xf_lease_valid(1,10,1,0,0,3600));
    { xf_keyboard k; char all[256]="";size_t n;
      xf_keyboard_init(&k,"",1);CHECK(k.secret&&k.selected==0);xf_keyboard_input(&k,'U');CHECK(k.selected==0);
      for(n=0;n<xf_keyboard_count(&k);++n){char b[8];const char *key=xf_keyboard_key(&k,n,b);if(strlen(key)==1)strcat(all,key);}
      xf_keyboard_input(&k,'Y');for(n=0;n<xf_keyboard_count(&k);++n){char b[8];const char *key=xf_keyboard_key(&k,n,b);if(strlen(key)==1)strcat(all,key);}
      for(n=33;n<127;++n)if(n<'A'||n>'Z')CHECK(strchr(all,(int)n));
      xf_keyboard_init(&k,"",1);for(n=0;n<200;++n)xf_keyboard_input(&k,'A');CHECK(strlen(k.value)==128);xf_keyboard_input(&k,'X');CHECK(strlen(k.value)==127);
      k.selected=xf_keyboard_count(&k)-1;CHECK(xf_keyboard_input(&k,'A')==1);xf_wipe(&k,sizeof(k));CHECK(!k.value[0]); }
    printf("Native protocol/JSON/DHCP predicate/keyboard: %u checks passed\n",checks);return 0;
}
