#pragma once
#include "network.hpp"
#include "settings.hpp"
#include <nlohmann/json.hpp>

namespace xf {
using Json=nlohmann::json;
struct Item {
    std::string id,name,type,overview,image_tag,artist,album;
    int year=0;
    int64_t duration_ticks=0,resume_ticks=0;
    bool folder=false;
    std::vector<std::string> genres;
};
struct Page { std::vector<Item> items; int total=0; };
struct Playback {
    std::string item_id,source_id,session_id,url,method;
    int64_t start_ticks=0;
    bool audio=false;
};
Item parse_item(const Json&);
Page parse_page(const Json&);
void apply_auth(Settings&,const Json&);
Json desktop_profile(int bitrate);
Playback select_playback(const Url&, const Item&, const Json&, int bitrate);
class Jellyfin {
    Transport& transport_;
    Settings& settings_;
public:
    Jellyfin(Transport& transport,Settings& settings) : transport_(transport),settings_(settings) {}
    Headers headers(bool json=true) const;
    Json api(const std::string& path,const std::string& method="GET",const Json& body=nullptr,Cancel cancel={});
    std::string connect(Cancel cancel={});
    void login(const std::string& username,const std::string& password,Cancel cancel={});
    void validate_session(Cancel cancel={});
    Page views(Cancel cancel={});
    Page items(const std::string& parent,int start=0,const std::string& sort="SortName",const std::string& types="",Cancel cancel={});
    Page search(const std::string& term,int start=0,Cancel cancel={});
    Item detail(const std::string& id,Cancel cancel={});
    Playback negotiate(const Item&,Cancel cancel={});
    void report(const Playback&,const std::string& event,int64_t ticks,bool paused);
    void stop_transcoding(const Playback&);
    std::string artwork_url(const Item&,int width=240) const;
};
}
