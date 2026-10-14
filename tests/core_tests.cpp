#include "xenonfin/jellyfin.hpp"
#include "xenonfin/keyboard.hpp"
#include "xenonfin/input.hpp"
#include <iostream>
#include <functional>
#include <algorithm>
#include <thread>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
using namespace xf;
#define CHECK(x) do {if(!(x)) throw std::runtime_error("Check failed: " #x);} while(0)
static void rejects(const std::function<void()>& f) {bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Fake final : Transport {
    Response response{200,"{}",{}};
    Request last;
    Response request(const Request& r) override {last=r;return response;}
};
static Json movie() {return {{"Id","film"},{"Name","A movie"},{"Type","Movie"},{"ProductionYear",2024},{"RunTimeTicks",40000000},{"ImageTags",{{"Primary","tag"}}},{"UserData",{{"PlaybackPositionTicks",10000000}}}};}
static Json playback() {
    return {{"PlaySessionId","session"},{"MediaSources",Json::array({{{"Id","source"},{"Container","mp4"},{"Bitrate",3000000},{"SupportsDirectPlay",true},
        {"MediaStreams",Json::array({{{"Type","Video"},{"Codec","h264"},{"Width",1280},{"Height",720},{"RealFrameRate",30}},{{"Type","Audio"},{"Codec","aac"},{"Channels",2}}})}}})}};
}
int main(int argc,char** argv) {
    int passed=0,failed=0;
    auto test=[&](const std::string& name,const std::function<void()>& f) {
        try{f();++passed;}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    };
    for(const auto& pair:std::vector<std::pair<std::string,std::string>>{
        {"192.168.68.202","http://192.168.68.202:8096"},{"192.168.68.202:8096","http://192.168.68.202:8096"},
        {"host.local","http://host.local:8096"},{"https://example.org","https://example.org"},{"https://example.org:443/jellyfin/","https://example.org/jellyfin"},
        {" http://example.org:8096/ ","http://example.org:8096"},{"[::1]:8096","http://[::1]:8096"}})
        test("URL normalize",[&]{CHECK(Url::parse(pair.first).base==pair.second);});
    for(const auto& input:{"","ftp://example.org","https://user:pw@example.org","https://x?api_key=secret","https://x/#fragment","host:0","host:65536","http://999.2.3.4","http://bad host","http://a..b","http://host/../private","http://x\r\nInjected: y"})
        test("URL reject",[&]{rejects([&]{Url::parse(input);});});
    test("URL same-origin",[]{auto u=Url::parse("https://host/jellyfin");CHECK(u.resolve("/Videos/x/stream")=="https://host/jellyfin/Videos/x/stream");CHECK(u.resolve("/jellyfin/Videos/x/stream")=="https://host/jellyfin/Videos/x/stream");rejects([&]{u.resolve("http://host/media");});rejects([&]{u.resolve("https://evil/media");});rejects([&]{u.resolve("//evil/media");});});
    test("Query escaping",[]{CHECK(encode("a &/é")=="a%20%26%2F%C3%A9");});
    test("JSON parsing",[]{CHECK(Json::parse("{\"Name\":\"雪\",\"Values\":[1,null]}")["Name"]=="雪");rejects([]{auto j=Json::parse("{bad}");(void)j;});});
    test("Auth success",[]{Settings s;apply_auth(s,{{"AccessToken","token"},{"User",{{"Id","user"},{"Name","Alex"}}}});CHECK(s.token=="token"&&s.user_id=="user"&&s.user_name=="Alex");});
    test("Auth atomic failure",[]{Settings s;s.token="old";rejects([&]{apply_auth(s,{{"AccessToken","new"},{"User",Json::object()}});});CHECK(s.token=="old");rejects([&]{apply_auth(s,{{"AccessToken",nullptr}});});});
    test("Auth no header injection",[]{Settings s;s.server="https://host";s.token="t\r\nInjected:bad";Fake f;Jellyfin api(f,s);rejects([&]{api.headers();});});
    test("Library metadata",[]{auto p=parse_page({{"Items",Json::array({movie()})},{"TotalRecordCount",45}});CHECK(p.total==45&&p.items.size()==1&&p.items[0].resume_ticks==10000000&&p.items[0].year==2024);});
    test("Library null fields",[]{auto j=movie();j["ProductionYear"]=nullptr;j["Genres"]=nullptr;j["Artists"]=nullptr;CHECK(parse_item(j).year==0);});
    test("Library invalid",[]{rejects([]{parse_page(Json::array());});rejects([]{parse_page({{"Items",Json::array({{{"Name","Missing ID"}}})}});});});
    test("Page size guard",[]{Json j={{"Items",Json::array()}};for(int i=0;i<201;++i)j["Items"].push_back(movie());rejects([&]{parse_page(j);});});
    test("Search actual API query",[]{Fake f;f.response.body="{\"Items\":[],\"TotalRecordCount\":0}";Settings s;s.server="https://host";s.user_id="user";Jellyfin api(f,s);auto p=api.search("song & movie",40);CHECK(p.items.empty());CHECK(f.last.url.find("SearchTerm=song%20%26%20movie")!=std::string::npos);CHECK(f.last.url.find("StartIndex=40")!=std::string::npos);});
    test("Search grouped types parse",[]{auto j=movie();j["Type"]="MusicArtist";auto p=parse_page({{"Items",Json::array({movie(),j})}});CHECK(p.items[1].type=="MusicArtist");});
    test("Profile limits",[]{auto j=desktop_profile(4000000);CHECK(j["MaxStreamingBitrate"]==4000000);CHECK(j["TranscodingProfiles"][0]["Protocol"]=="http");CHECK(j["CodecProfiles"][0]["Conditions"][0]["Value"]=="720");});
    test("Playback direct",[]{auto p=select_playback(Url::parse("https://host"),parse_item(movie()),playback(),4000000);CHECK(p.method=="DirectPlay");CHECK(p.url.find("/Videos/film/stream?Static=true")!=std::string::npos);CHECK(p.start_ticks==10000000);});
    test("Playback HEVC fallback",[]{auto j=playback();auto& s=j["MediaSources"][0];s["MediaStreams"][0]["Codec"]="hevc";s["SupportsTranscoding"]=true;s["TranscodingUrl"]="/Videos/film/stream.ts?VideoCodec=h264";CHECK(select_playback(Url::parse("https://host"),parse_item(movie()),j,4000000).method=="Transcode");});
    test("Playback HDR rejects direct",[]{auto j=playback();j["MediaSources"][0]["MediaStreams"][0]["BitDepth"]=10;rejects([&]{select_playback(Url::parse("https://host"),parse_item(movie()),j,4000000);});});
    test("Playback 1080p rejects direct",[]{auto j=playback();j["MediaSources"][0]["MediaStreams"][0]["Height"]=1080;rejects([&]{select_playback(Url::parse("https://host"),parse_item(movie()),j,4000000);});});
    test("Playback no source",[]{rejects([]{select_playback(Url::parse("https://host"),parse_item(movie()),{{"PlaySessionId","s"},{"MediaSources",Json::array()}},4000000);});});
    test("Playback HLS not falsely supported",[]{auto j=playback();auto& s=j["MediaSources"][0];s["SupportsDirectPlay"]=false;s["SupportsTranscoding"]=true;s["TranscodingUrl"]="/master.m3u8";s["TranscodingSubProtocol"]="hls";rejects([&]{select_playback(Url::parse("https://host"),parse_item(movie()),j,4000000);});});
    test("Playback cross-origin refused",[]{auto j=playback();auto& s=j["MediaSources"][0];s["SupportsDirectPlay"]=false;s["SupportsTranscoding"]=true;s["TranscodingUrl"]="https://evil/stream.ts";rejects([&]{select_playback(Url::parse("https://host"),parse_item(movie()),j,4000000);});});
    test("Network HTTP failure",[]{for(int n:{401,403,404,500})rejects([&]{check_response({n,"",{}});});});
    test("Network JSON failure",[]{Fake f;f.response.body="not json";Settings s;s.server="https://host";Jellyfin api(f,s);rejects([&]{api.connect();});});
    test("Network server identity",[]{Fake f;f.response.body="{\"ServerName\":\"An unrelated API\"}";Settings s;s.server="https://host";Jellyfin api(f,s);rejects([&]{api.connect();});});
    test("Auth sends supported fields",[]{Fake f;f.response.body="{\"AccessToken\":\"token\",\"User\":{\"Id\":\"user\"}}";Settings s;s.server="https://host";s.device_id="d";Jellyfin api(f,s);api.login("alex","fixture-password");CHECK(Json::parse(f.last.body)["Pw"]=="fixture-password");CHECK(f.last.headers[0].find("MediaBrowser Client=")!=std::string::npos);});
    test("Pagination limit",[]{Fake f;f.response.body="{\"Items\":[]}";Settings s;s.server="https://host";Jellyfin api(f,s);api.items("folder",80);CHECK(f.last.url.find("Limit=40")!=std::string::npos&&f.last.url.find("StartIndex=80")!=std::string::npos);});
    test("Reports negotiated session",[]{Fake f;f.response.body="";Settings s;s.server="https://host";Jellyfin api(f,s);Playback p;p.session_id="s";p.source_id="m";p.item_id="i";p.method="DirectPlay";api.report(p,"Playing/Progress",123,true);auto j=Json::parse(f.last.body);CHECK(j["PlaySessionId"]=="s"&&j["PositionTicks"]==123&&j["IsPaused"]==true);});
    test("Keyboard secret masking",[]{Keyboard k("Password","éA",true);CHECK(k.display()=="**");k.erase();CHECK(k.value=="é");k.erase();CHECK(k.value.empty());});
    test("Keyboard controller full charset",[]{Keyboard k;std::string chars;for(const auto& v:k.keys)if(v.size()==1)chars+=v;k.input(Action::Info);for(const auto& v:k.keys)if(v.size()==1)chars+=v;for(char c=33;c<127;++c)if(!(c>='A'&&c<='Z'))CHECK(chars.find(c)!=std::string::npos);});
    test("Keyboard bounds and completion",[]{Keyboard k;k.input(Action::Up);CHECK(k.selected==0);k.input(Action::Down);CHECK(k.selected==10);k.input(Action::Start);CHECK(k.done);});
    char dir[]="/tmp/xenonfin-tests-XXXXXX";CHECK(mkdtemp(dir));std::string path=std::string(dir)+"/settings.json";
    test("Settings default device ID",[&]{auto s=Settings::load(path);CHECK(s.device_id.size()==32);});
    test("Settings roundtrip permissions",[&]{auto s=Settings::load(path);s.server="http://host:8096";s.token="fixture-token";s.user_id="user";s.save(path);auto loaded=Settings::load(path);CHECK(loaded.server==s.server&&loaded.token==s.token&&loaded.device_id==s.device_id);struct stat st{};CHECK(stat(path.c_str(),&st)==0&&(st.st_mode&0777)==0600);s.save(path);CHECK(Settings::load(path).token==s.token);});
    test("Settings do not store passwords",[&]{std::ifstream f(path);std::string text((std::istreambuf_iterator<char>(f)),{});CHECK(text.find("password")==std::string::npos&&text.find("Pw")==std::string::npos);});
    test("Settings opt out",[&]{auto s=Settings::load(path);s.remember_login=false;s.save(path);CHECK(Settings::load(path).token.empty());});
    test("Settings logout",[&]{auto s=Settings::load(path);s.logout();s.save(path);CHECK(Settings::load(path).user_id.empty());});
    test("Settings reject permissive file",[&]{chmod(path.c_str(),0644);rejects([&]{Settings::load(path);});chmod(path.c_str(),0600);});
    test("Settings reject symlink",[&]{auto link=path+".link";symlink(path.c_str(),link.c_str());rejects([&]{Settings::load(link);});});
    test("Settings invalid JSON",[&]{std::ofstream f(path);f<<"{bad";f.close();rejects([&]{Settings::load(path);});});
    std::filesystem::remove_all(dir);
    if(argc>1) {
        std::string base=argv[1];Http http;
        test("HTTP bounded response",[&]{Request r;r.url=base+"/oversize";r.max_bytes=128;rejects([&]{http.request(r);});});
        test("HTTP cancel",[&]{Request r;r.url=base+"/slow";r.cancel=std::make_shared<std::atomic<bool>>(false);std::thread t([&]{usleep(100000);r.cancel->store(true);});rejects([&]{http.request(r);});t.join();});
        test("HTTP retry after reset",[&]{Request r;r.url=base+"/reset";CHECK(http.request(r).body=="recovered");});
        test("HTTP same origin redirect",[&]{Request r;r.url=base+"/redirect";CHECK(http.request(r).body=="recovered");});
        test("HTTP cross origin redirect",[&]{Request r;r.url=base+"/foreign";r.headers={"Authorization: MediaBrowser Token=\"fixture\""};rejects([&]{http.request(r);});});
        test("HTTP POST redirect refusal",[&]{Request r;r.url=base+"/redirect";r.method="POST";r.body="private";rejects([&]{http.request(r);});});
    }
    test("Controller A and held D-pad repeat",[]{
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
        SDL_VirtualJoystickDesc desc{};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=SDL_CONTROLLER_AXIS_MAX;desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;desc.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;desc.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;
        int index=SDL_JoystickAttachVirtualEx(&desc);CHECK(index>=0);
        auto* joystick=SDL_JoystickOpen(index);CHECK(joystick);
        {Input input;input.poll();SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_A,1);SDL_JoystickUpdate();auto actions=input.poll();CHECK(std::find(actions.begin(),actions.end(),Action::Select)!=actions.end());
         SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);SDL_JoystickUpdate();actions=input.poll();CHECK(std::find(actions.begin(),actions.end(),Action::Down)!=actions.end());CHECK(input.poll().empty());SDL_Delay(370);actions=input.poll();CHECK(std::find(actions.begin(),actions.end(),Action::Down)!=actions.end());}
        SDL_JoystickClose(joystick);SDL_JoystickDetachVirtual(index);SDL_Quit();
    });
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
