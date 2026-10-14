#pragma once
#include <string>
namespace xf {
struct Settings {
    std::string server, token, user_id, user_name, device_id;
    int video_bitrate=4000000;
    bool remember_login=true;
    static std::string default_path();
    static Settings load(const std::string& path);
    void save(const std::string& path) const;
    void logout() { token.clear(); user_id.clear(); user_name.clear(); }
};
}
