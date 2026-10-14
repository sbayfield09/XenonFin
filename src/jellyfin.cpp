#include "xenonfin/jellyfin.hpp"
#include <algorithm>

namespace xf {
template<class T> static T val(const Json& j,const char* key,T fallback) {
    auto it=j.find(key);
    return it==j.end() || it->is_null() ? fallback : it->get<T>();
}
static std::string required(const Json& j,const char* key) {
    auto s=val(j,key,std::string{});
    if(s.empty()||s.size()>1024 || s.find_first_of("\r\n")!=std::string::npos) throw Error(std::string("Invalid server field: ")+key);
    return s;
}
Item parse_item(const Json& j) {
    if(!j.is_object()) throw Error("Invalid media item");
    Item i;
    i.id=required(j,"Id"); i.name=val(j,"Name",std::string{"Untitled"}); i.type=val(j,"Type",std::string{});
    i.overview=val(j,"Overview",std::string{}); i.year=val(j,"ProductionYear",0);
    i.duration_ticks=val(j,"RunTimeTicks",int64_t{0}); i.folder=val(j,"IsFolder",false);
    i.album=val(j,"Album",std::string{});
    auto images=j.find("ImageTags");
    if(images!=j.end() && images->is_object()) i.image_tag=val(*images,"Primary",std::string{});
    auto user=j.find("UserData");
    if(user!=j.end() && user->is_object()) i.resume_ticks=val(*user,"PlaybackPositionTicks",int64_t{0});
    auto artists=val(j,"Artists",std::vector<std::string>{});
    if(!artists.empty()) i.artist=artists.front();
    i.genres=val(j,"Genres",std::vector<std::string>{});
    return i;
}
Page parse_page(const Json& j) {
    if(!j.is_object() || !j.contains("Items") || !j["Items"].is_array()) throw Error("Invalid library response");
    if(j["Items"].size()>200) throw Error("Library page exceeds requested memory limit");
    Page p;
    for(const auto& v:j["Items"]) p.items.push_back(parse_item(v));
    p.total=val(j,"TotalRecordCount",static_cast<int>(p.items.size()));
    return p;
}
void apply_auth(Settings& s,const Json& j) {
    if(!j.is_object() || !j.contains("User") || !j["User"].is_object()) throw Error("Invalid authentication response");
    // Validate all fields before modifying a saved session.
    auto token=required(j,"AccessToken"), id=required(j["User"],"Id"), name=val(j["User"],"Name",std::string{});
    s.token=token; s.user_id=id; s.user_name=name;
}
Json desktop_profile(int bitrate) {
    // This describes the desktop FFmpeg backend, never the unintegrated Xenon target.
    Json condition={{"Condition","LessThanEqual"},{"Property","Height"},{"Value","720"},{"IsRequired",true}};
    return {{"Name","XenonFin desktop FFmpeg"},{"MaxStreamingBitrate",bitrate},{"MaxStaticBitrate",bitrate},
        {"MusicStreamingTranscodingBitrate",192000},
        {"DirectPlayProfiles",Json::array({
            {{"Type","Video"},{"Container","mp4,m4v,mov"},{"VideoCodec","h264"},{"AudioCodec","aac"}},
            {{"Type","Audio"},{"Container","mp3"},{"AudioCodec","mp3"}},
            {{"Type","Audio"},{"Container","m4a,aac"},{"AudioCodec","aac"}},
            {{"Type","Audio"},{"Container","flac"},{"AudioCodec","flac"}}})},
        {"TranscodingProfiles",Json::array({
            {{"Type","Video"},{"Container","ts"},{"Protocol","http"},{"VideoCodec","h264"},{"AudioCodec","aac"},{"Context","Streaming"},{"MaxAudioChannels","2"},{"MinSegments",1},{"BreakOnNonKeyFrames",true}},
            {{"Type","Audio"},{"Container","mp3"},{"Protocol","http"},{"AudioCodec","mp3"},{"Context","Streaming"},{"MaxAudioChannels","2"}}})},
        {"CodecProfiles",Json::array({
            {{"Type","Video"},{"Codec","h264"},{"Conditions",Json::array({condition,
                {{"Condition","LessThanEqual"},{"Property","Width"},{"Value","1280"},{"IsRequired",true}},
                {{"Condition","LessThanEqual"},{"Property","VideoFramerate"},{"Value","30"},{"IsRequired",false}},
                {{"Condition","EqualsAny"},{"Property","VideoProfile"},{"Value","baseline|constrained baseline|main|high"},{"IsRequired",false}},
                {{"Condition","LessThanEqual"},{"Property","VideoBitDepth"},{"Value","8"},{"IsRequired",false}},
                {{"Condition","Equals"},{"Property","VideoRangeType"},{"Value","SDR"},{"IsRequired",false}}})}},
            {{"Type","VideoAudio"},{"Codec","aac"},{"Conditions",Json::array({{{"Condition","LessThanEqual"},{"Property","AudioChannels"},{"Value","2"},{"IsRequired",true}}})}}})},
        {"SubtitleProfiles",Json::array()}};
}
static bool direct_supported(const Json& source,bool audio,int bitrate) {
    auto container=val(source,"Container",std::string{});
    if(val(source,"Bitrate",0)>bitrate && !audio) return false;
    bool has_audio=false,has_video=false;
    auto streams=source.find("MediaStreams");
    if(streams==source.end()||!streams->is_array()) return false;
    for(const auto& stream:*streams) {
        auto type=val(stream,"Type",std::string{}),codec=val(stream,"Codec",std::string{});
        if(type=="Video" && !val(stream,"IsAttachedPic",false)) {
            if(audio) return false;
            has_video=true;
            if(codec!="h264" || val(stream,"Width",99999)>1280 || val(stream,"Height",99999)>720 ||
               val(stream,"RealFrameRate",0.0)>30.1 || val(stream,"BitDepth",8)>8 ||
               val(stream,"VideoRange",std::string{"SDR"})!="SDR") return false;
        }
        if(type=="Audio") {
            has_audio=true;
            if(audio) { if(codec!="mp3" && codec!="aac" && codec!="flac") return false; }
            else if(codec!="aac" || val(stream,"Channels",99)>2) return false;
        }
    }
    if(audio) return has_audio && (container=="mp3" || container=="aac" || container=="m4a" || container=="flac");
    return has_video && (container=="mp4" || container=="m4v" || container=="mov");
}
Playback select_playback(const Url& server,const Item& item,const Json& j,int bitrate) {
    if(j.contains("ErrorCode") && !j["ErrorCode"].is_null()) throw Error("Server cannot negotiate a supported playback format");
    if(!j.contains("MediaSources") || !j["MediaSources"].is_array()) throw Error("No playable media sources");
    Playback p; p.item_id=item.id; p.audio=item.type=="Audio"; p.start_ticks=item.resume_ticks;
    p.session_id=required(j,"PlaySessionId");
    for(const auto& source:j["MediaSources"]) {
        p.source_id=required(source,"Id");
        if(val(source,"SupportsDirectPlay",false) && direct_supported(source,p.audio,bitrate)) {
            // Documented static-stream endpoints, used only after positive server negotiation.
            p.url=server.base+(p.audio?"/Audio/":"/Videos/")+encode(item.id)+"/stream?Static=true&MediaSourceId="+encode(p.source_id)+"&PlaySessionId="+encode(p.session_id);
            p.method="DirectPlay"; return p;
        }
        auto url=val(source,"TranscodingUrl",std::string{});
        if(!url.empty() && val(source,"SupportsTranscoding",false)) {
            if(val(source,"TranscodingSubProtocol",std::string{"http"})!="http" || url.find(".m3u8")!=std::string::npos)
                continue; // Nested HLS fetches are outside our authenticated AVIO transport.
            p.url=server.resolve(url); p.method="Transcode"; return p;
        }
    }
    throw Error("Server offered no supported progressive stream; check transcoding permissions");
}
Headers Jellyfin::headers(bool json) const {
    auto q=[](const std::string& s) {
        if(s.find_first_of("\r\n")!=std::string::npos || s.size()>1024) throw Error("Invalid authentication field");
        return Json(s).dump();
    };
    std::string auth="Authorization: MediaBrowser Client=\"XenonFin\", Device=\"Desktop FFmpeg\", DeviceId="+q(settings_.device_id)+", Version=\"0.1.0\"";
    if(!settings_.token.empty()) auth+=", Token="+q(settings_.token);
    Headers h={auth,"Accept: application/json"};
    if(json) h.push_back("Content-Type: application/json");
    return h;
}
Json Jellyfin::api(const std::string& path,const std::string& method,const Json& body,Cancel cancel) {
    auto url=Url::parse(settings_.server).base+path;
    Request r; r.url=url; r.method=method; r.headers=headers(); r.cancel=cancel;
    if(!body.is_null()) r.body=body.dump();
    auto response=transport_.request(r); check_response(response);
    if(response.body.empty()) return nullptr;
    try { return Json::parse(response.body); }
    catch(const Json::exception&) { throw Error("Server returned invalid JSON"); }
}
std::string Jellyfin::connect(Cancel c) {
    settings_.server=Url::parse(settings_.server).base;
    auto j=api("/System/Info/Public","GET",nullptr,c);
    if(!j.is_object() || !j.contains("Version") || !j["Version"].is_string()) throw Error("Address did not return Jellyfin server information");
    return val(j,"ServerName",std::string{"Jellyfin"});
}
void Jellyfin::login(const std::string& username,const std::string& password,Cancel c) {
    if(username.empty()) throw Error("Enter a username");
    auto j=api("/Users/AuthenticateByName","POST",{{"Username",username},{"Pw",password}},c);
    try { apply_auth(settings_,j); }
    catch(const Json::exception&) { throw Error("Invalid authentication response"); }
}
void Jellyfin::validate_session(Cancel c) {
    if(settings_.user_id.empty() || settings_.token.empty()) throw Error("Log in to continue",401);
    auto j=api("/Users/"+encode(settings_.user_id),"GET",nullptr,c);
    if(required(j,"Id")!=settings_.user_id) throw Error("Saved session belongs to another user",401);
    settings_.user_name=val(j,"Name",std::string{});
}
Page Jellyfin::views(Cancel c) { return parse_page(api("/Users/"+encode(settings_.user_id)+"/Views","GET",nullptr,c)); }
Page Jellyfin::items(const std::string& parent,int start,const std::string& sort,const std::string& types,Cancel c) {
    std::string q="/Users/"+encode(settings_.user_id)+"/Items?StartIndex="+std::to_string(std::max(0,start))+"&Limit=40&Fields=Overview,Genres,MediaSources&SortBy="+encode(sort)+"&SortOrder="+(sort=="SortName"?"Ascending":"Descending");
    if(!parent.empty()) q+="&ParentId="+encode(parent);
    if(!types.empty()) q+="&Recursive=true&IncludeItemTypes="+encode(types);
    return parse_page(api(q,"GET",nullptr,c));
}
Page Jellyfin::search(const std::string& term,int start,Cancel c) {
    if(term.empty()) return {};
    return parse_page(api("/Users/"+encode(settings_.user_id)+"/Items?Recursive=true&Limit=40&StartIndex="+std::to_string(std::max(0,start))+"&IncludeItemTypes=Movie,Series,Episode,Audio,MusicAlbum,MusicArtist&Fields=Overview,Genres&SearchTerm="+encode(term),"GET",nullptr,c));
}
Item Jellyfin::detail(const std::string& id,Cancel c) { return parse_item(api("/Users/"+encode(settings_.user_id)+"/Items/"+encode(id),"GET",nullptr,c)); }
Playback Jellyfin::negotiate(const Item& i,Cancel c) {
    Json request={{"UserId",settings_.user_id},{"DeviceProfile",desktop_profile(settings_.video_bitrate)},
        {"MaxStreamingBitrate",settings_.video_bitrate},{"StartTimeTicks",i.resume_ticks},
        {"EnableDirectPlay",true},{"EnableDirectStream",false},{"EnableTranscoding",true},
        {"AllowVideoStreamCopy",false},{"AllowAudioStreamCopy",false},{"IsPlayback",true},{"AutoOpenLiveStream",true}};
    auto j=api("/Items/"+encode(i.id)+"/PlaybackInfo","POST",request,c);
    return select_playback(Url::parse(settings_.server),i,j,settings_.video_bitrate);
}
void Jellyfin::report(const Playback& p,const std::string& event,int64_t ticks,bool paused) {
    if(event!="Playing" && event!="Playing/Progress" && event!="Playing/Stopped") throw Error("Invalid playback event");
    Json body={{"ItemId",p.item_id},{"MediaSourceId",p.source_id},{"PlaySessionId",p.session_id},
        {"PositionTicks",std::max(int64_t{0},ticks)},{"IsPaused",paused},{"CanSeek",true},{"PlayMethod",p.method},{"VolumeLevel",100},{"IsMuted",false},{"RepeatMode","RepeatNone"}};
    api("/Sessions/"+event,"POST",body);
}
void Jellyfin::stop_transcoding(const Playback& p) {
    if(p.method=="Transcode") api("/Videos/ActiveEncodings?DeviceId="+encode(settings_.device_id)+"&PlaySessionId="+encode(p.session_id),"DELETE");
}
std::string Jellyfin::artwork_url(const Item& i,int width) const {
    if(i.image_tag.empty()) return {};
    return Url::parse(settings_.server).base+"/Items/"+encode(i.id)+"/Images/Primary?MaxWidth="+std::to_string(width)+"&Quality=80&Tag="+encode(i.image_tag);
}
}
