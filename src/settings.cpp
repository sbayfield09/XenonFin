#include "xenonfin/settings.hpp"
#include "xenonfin/network.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace xf {
static std::string random_id() {
    std::random_device r;
    std::string s;
    for(int i=0;i<32;++i) s+="0123456789abcdef"[r()&15];
    return s;
}
std::string Settings::default_path() {
    if(const char* p=std::getenv("XENONFIN_SETTINGS")) return p;
    if(const char* p=std::getenv("XDG_CONFIG_HOME")) return std::string(p)+"/xenonfin/settings.json";
    if(const char* p=std::getenv("HOME")) return std::string(p)+"/.config/xenonfin/settings.json";
    throw Error("Set XENONFIN_SETTINGS to a writable configuration path");
}
Settings Settings::load(const std::string& path) {
    Settings s; s.device_id=random_id();
    struct stat st{};
    if(lstat(path.c_str(),&st)!=0) {
        if(errno==ENOENT) return s;
        throw Error("Cannot inspect saved configuration");
    }
    if(!S_ISREG(st.st_mode)||st.st_size>16384 || (st.st_mode&0077) || st.st_uid!=getuid())
        throw Error("Configuration must be a private regular file owned by this user (chmod 600)");
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0) throw Error("Cannot open saved configuration");
    std::string data;
    char buf[4096]; ssize_t n;
    while((n=read(fd,buf,sizeof(buf)))>0 && data.size()<16384) data.append(buf,n);
    close(fd);
    if(n<0 || data.size()>16384) throw Error("Cannot read saved configuration");
    try {
        auto j=nlohmann::json::parse(data);
        s.server=j.value("server",std::string{});
        if(!s.server.empty()) s.server=Url::parse(s.server).base;
        s.device_id=j.value("device_id",s.device_id);
        s.remember_login=j.value("remember_login",true);
        if(s.remember_login) {
            s.token=j.value("token",std::string{}); s.user_id=j.value("user_id",std::string{}); s.user_name=j.value("user_name",std::string{});
        }
        s.video_bitrate=j.value("video_bitrate",4000000);
        if(s.video_bitrate<500000 || s.video_bitrate>10000000) s.video_bitrate=4000000;
    } catch(const nlohmann::json::exception&) { throw Error("Saved configuration is invalid JSON"); }
    return s;
}
void Settings::save(const std::string& path) const {
    namespace fs=std::filesystem;
    auto parent=fs::path(path).parent_path();
    if(parent.empty()) parent=".";
    if(!fs::exists(parent)) { fs::create_directories(parent); chmod(parent.c_str(),0700); }
    auto tmp=path+".tmp."+random_id();
    int fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0) throw Error("Cannot create private configuration file");
    nlohmann::json j={{"server",server},{"device_id",device_id},{"video_bitrate",video_bitrate},{"remember_login",remember_login},
        {"token",remember_login?token:""},{"user_id",remember_login?user_id:""},{"user_name",remember_login?user_name:""}};
    auto bytes=j.dump(2);
    size_t offset=0;
    while(offset<bytes.size()) {
        auto n=write(fd,bytes.data()+offset,bytes.size()-offset);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) { close(fd); unlink(tmp.c_str()); throw Error("Cannot write configuration"); }
        offset+=n;
    }
    int sync_result=fsync(fd), close_result=close(fd);
    if(sync_result!=0 || close_result!=0 || rename(tmp.c_str(),path.c_str())!=0) {
        unlink(tmp.c_str()); throw Error("Cannot persist configuration");
    }
    int dir=open(parent.c_str(),O_RDONLY|O_DIRECTORY);
    if(dir>=0) { fsync(dir); close(dir); }
}
}
