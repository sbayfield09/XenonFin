#include "xenonfin/network.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <mutex>
#include <thread>

namespace xf {
static std::once_flag curl_init_flag;
Http::Http() {
    std::call_once(curl_init_flag, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw Error("HTTP initialization failed");
    });
}
Http::~Http() {if(handle_)curl_easy_cleanup(static_cast<CURL*>(handle_));}
static std::string part(CURLU* u, CURLUPart p) {
    char* s = nullptr;
    if (curl_url_get(u, p, &s, 0) != CURLUE_OK) return {};
    std::string result(s);
    curl_free(s);
    return result;
}
Url Url::parse(std::string input) {
    Http init;
    auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) throw Error("Enter a server address");
    input = input.substr(first, input.find_last_not_of(" \t\r\n") - first + 1);
    if (input.size() > 2048 || std::any_of(input.begin(), input.end(), [](unsigned char c) { return c <= 32 || c == 127; }))
        throw Error("Invalid server address");
    bool implicit = input.find("://") == std::string::npos;
    if (implicit) input = "http://" + input;
    auto u = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>(curl_url(), curl_url_cleanup);
    if (!u || curl_url_set(u.get(), CURLUPART_URL, input.c_str(), CURLU_PATH_AS_IS) != CURLUE_OK)
        throw Error("Invalid server URL");
    std::string scheme = part(u.get(), CURLUPART_SCHEME), host = part(u.get(), CURLUPART_HOST);
    if ((scheme != "http" && scheme != "https") || host.empty() || !part(u.get(), CURLUPART_USER).empty() ||
        !part(u.get(), CURLUPART_PASSWORD).empty() || !part(u.get(), CURLUPART_QUERY).empty() || !part(u.get(), CURLUPART_FRAGMENT).empty() || input.find('@') != std::string::npos)
        throw Error("Use an HTTP(S) URL without credentials, query, or fragment");
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
    if (host.front() == '[') {
        in6_addr addr{};
        if (inet_pton(AF_INET6, host.substr(1,host.size()-2).c_str(), &addr) != 1) throw Error("Invalid IPv6 address");
    } else {
        if (host.size() > 253 || host.front() == '.' || host.back() == '.' || host.front() == '-' || host.back() == '-' || host.find("..") != std::string::npos)
            throw Error("Invalid hostname");
        if (std::any_of(host.begin(),host.end(),[](unsigned char c) { return !std::isalnum(c) && c!='.' && c!='-'; })) throw Error("Invalid hostname");
        if (std::all_of(host.begin(),host.end(),[](unsigned char c) { return std::isdigit(c) || c=='.'; })) {
            in_addr addr{};
            if (inet_pton(AF_INET,host.c_str(),&addr)!=1) throw Error("Invalid IPv4 address");
        }
    }
    auto port = part(u.get(), CURLUPART_PORT);
    if (implicit && port.empty()) port = "8096";
    if (!port.empty()) {
        if (!std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); }) || port.size() > 5 || std::stoi(port) < 1 || std::stoi(port) > 65535)
            throw Error("Invalid port");
    }
    std::string path = part(u.get(), CURLUPART_PATH);
    if (path.find("/../") != std::string::npos || path.find("/./") != std::string::npos || path.find('\\') != std::string::npos)
        throw Error("Invalid base path");
    while (!path.empty() && path.back() == '/') path.pop_back();
    Url out;
    out.origin = scheme + "://" + host + (port.empty() || (scheme=="http" && port=="80") || (scheme=="https" && port=="443") ? "" : ":"+port);
    out.base = out.origin + path;
    return out;
}
std::string Url::resolve(const std::string& relative) const {
    if (relative.empty() || relative.find_first_of("\r\n") != std::string::npos) throw Error("Invalid media URL");
    if (relative.find("://") != std::string::npos) {
        auto split = relative.find_first_of("/?",relative.find("://")+3);
        auto authority = split == std::string::npos ? relative : relative.substr(0,split);
        if (Url::parse(authority).origin != origin) throw Error("Refusing credentials to a different media origin");
        return relative;
    }
    if (relative.rfind("//",0) == 0) throw Error("Invalid cross-origin URL");
    // Jellyfin returns paths relative to its application base, including BaseUrl installations.
    auto basepath = base.substr(origin.size());
    if (!basepath.empty() && relative.rfind(basepath+"/",0)==0) return origin+relative;
    return base + (relative.front()=='/' ? "" : "/") + relative;
}
std::string encode(const std::string& input) {
    static const char* digits="0123456789ABCDEF";
    std::string out;
    for (unsigned char c : input) {
        if ((std::isalnum(c) && c<128) || c=='-' || c=='_' || c=='.' || c=='~') out+=c;
        else { out+='%'; out+=digits[c>>4]; out+=digits[c&15]; }
    }
    return out;
}
namespace {
struct Transfer { const Request* request; Response* response; bool overflow=false; };
size_t write_body(char* p,size_t a,size_t b,void* opaque) {
    auto& t=*static_cast<Transfer*>(opaque);
    size_t n=a*b;
    if (n>t.request->max_bytes || t.response->body.size()>t.request->max_bytes-n) { t.overflow=true; return 0; }
    t.response->body.append(p,n); return n;
}
size_t write_header(char* p,size_t a,size_t b,void* opaque) {
    auto& response=*static_cast<Response*>(opaque);
    std::string line(p,a*b);
    auto colon=line.find(':');
    if (colon!=std::string::npos) {
        auto key=line.substr(0,colon), val=line.substr(colon+1);
        std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return std::tolower(c);});
        auto start=val.find_first_not_of(" \t\r\n"), end=val.find_last_not_of(" \t\r\n");
        response.headers[key]=start==std::string::npos ? "" : val.substr(start,end-start+1);
    }
    return a*b;
}
int progress(void* opaque,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    auto* c=static_cast<const Request*>(opaque);
    return c->cancel && c->cancel->load();
}
}
Response Http::request(const Request& initial) {
    std::lock_guard<std::mutex> lock(mutex_);
    Request r=initial;
    if (r.method!="GET" && r.method!="HEAD" && r.method!="POST" && r.method!="DELETE") throw Error("Unsupported HTTP method");
    for (const auto& h:r.headers) if (h.find_first_of("\r\n")!=std::string::npos) throw Error("Invalid HTTP header");
    auto authority = r.url.substr(0,r.url.find_first_of("/?",r.url.find("://")+3));
    Url server=Url::parse(authority);
    for (int attempt=0; attempt<5; ++attempt) {
        if (r.cancel && r.cancel->load()) throw Error("Request cancelled");
        if(!handle_)handle_=curl_easy_init();
        auto* c=static_cast<CURL*>(handle_);
        if(!c) throw Error("HTTP allocation failed");
        curl_easy_reset(c); // Retains libcurl's connection, DNS and TLS session caches.
        Response response;
        Transfer t{&r,&response};
        curl_slist* raw=nullptr;
        for(const auto& h:r.headers) raw=curl_slist_append(raw,h.c_str());
        auto hs=std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)>(raw,curl_slist_free_all);
        curl_easy_setopt(c,CURLOPT_URL,r.url.c_str());
        curl_easy_setopt(c,CURLOPT_HTTPHEADER,hs.get());
        curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,8L);
        curl_easy_setopt(c,CURLOPT_TIMEOUT,r.timeout_seconds);
        curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"http,https");
        curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
        curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);
        curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
        curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,write_body);
        curl_easy_setopt(c,CURLOPT_WRITEDATA,&t);
        curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,write_header);
        curl_easy_setopt(c,CURLOPT_HEADERDATA,&response);
        curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);
        curl_easy_setopt(c,CURLOPT_XFERINFODATA,&r);
        curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
        if (r.method=="POST") {
            curl_easy_setopt(c,CURLOPT_POST,1L);
            curl_easy_setopt(c,CURLOPT_POSTFIELDS,r.body.data());
            curl_easy_setopt(c,CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(r.body.size()));
        } else if (r.method=="HEAD") curl_easy_setopt(c,CURLOPT_NOBODY,1L);
        else if (r.method=="DELETE") curl_easy_setopt(c,CURLOPT_CUSTOMREQUEST,"DELETE");
        auto status=curl_easy_perform(c);
        curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&response.status);
        if(status!=CURLE_OK) {
            if(r.cancel && r.cancel->load()) throw Error("Request cancelled");
            if(t.overflow) throw Error("Server response exceeds memory limit");
            if(attempt==0 && (r.method=="GET"||r.method=="HEAD") && (status==CURLE_RECV_ERROR||status==CURLE_COULDNT_CONNECT||status==CURLE_GOT_NOTHING||status==CURLE_SEND_ERROR)) continue;
            throw Error(std::string("Network request failed: ")+curl_easy_strerror(status));
        }
        if(response.status>=300 && response.status<400) {
            auto it=response.headers.find("location");
            if(it==response.headers.end()) throw Error("Redirect has no location");
            if(r.method!="GET" && r.method!="HEAD") throw Error("Refusing to redirect a credential submission");
            r.url=server.resolve(it->second);
            continue;
        }
        return response;
    }
    throw Error("Too many redirects");
}
void check_response(const Response& r) {
    if(r.status==401||r.status==403) throw Error("Authentication or access denied; log in again",static_cast<int>(r.status));
    if(r.status<200||r.status>=300) throw Error("Server returned HTTP "+std::to_string(r.status),static_cast<int>(r.status));
}
}
