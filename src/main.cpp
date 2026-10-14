#include "xenonfin/ui.hpp"
#include <iostream>
#include <cstdlib>

namespace xf {
static Settings credentials(const std::string& server) {
    Settings settings=Settings::load(Settings::default_path());
    auto target=Url::parse(server).base;
    if(settings.server!=target)settings.logout();
    settings.server=target;
    if(const char* p=std::getenv("JELLYFIN_ACCESS_TOKEN")) settings.token=p;
    if(const char* p=std::getenv("JELLYFIN_USER_ID")) settings.user_id=p;
    return settings;
}
static int play(const std::string& path,const Headers& headers,bool headless,double limit,bool exercise=false,double start=0,bool transcoded=false,std::function<void(double)> tick={},double* final_position=nullptr,const std::string& method="") {
    Graphics graphics(headless);
    Input input;
    Player player;
    player.open(path,headers,start,transcoded);
    auto began=SDL_GetTicks(); VideoFrame frame; bool sought=false,paused=false,resumed=false,quit=false;
    while(!player.finished()&&!quit) {
        auto elapsed=(SDL_GetTicks()-began)/1000.0;
        if(exercise && elapsed>=0.5 && !sought) {player.seek(1.0);sought=true;}
        if(exercise && elapsed>=0.8 && !paused) {player.pause();paused=true;}
        if(exercise && elapsed>=1.0 && !resumed) {player.pause();resumed=true;}
        if(tick && !player.buffering()) tick(player.position());
        for(auto action:input.poll()) {
            if(action==Action::Quit||action==Action::Back) quit=true;
            if(action==Action::Select) player.pause();
            if(action==Action::SeekForward) player.seek(player.position()+10);
            if(action==Action::SeekBack) player.seek(std::max(0.0,player.position()-10));
        }
        player.next(frame); graphics.begin(); graphics.video(frame); graphics.present(); SDL_Delay(5);
        if(limit>0 && elapsed>=limit) quit=true;
    }
    auto error=player.error(); auto position=player.position(); player.stop();auto m=player.metrics();
    if(final_position) *final_position=position;
    Json output={{"decoded_video_frames",m.decoded},{"displayed_video_frames",m.displayed},{"dropped_video_frames",m.dropped},
        {"video_codec",m.video_codec},{"audio_codec",m.audio_codec},{"container",m.container},{"width",m.width},{"height",m.height},{"play_method",method},
        {"decoded_audio_samples",m.audio_samples},
        {"audio_underruns",m.underruns},{"compressed_bytes",m.bytes},{"wall_seconds",m.seconds},
        {"display_fps",m.seconds>0?m.displayed/m.seconds:0},{"max_video_to_audio_clock_drift_seconds",m.max_av_drift},
        {"final_position_seconds",position},{"seek_exercised",sought},{"pause_exercised",paused&&resumed},
        {"backend","desktop SDL/FFmpeg"},{"error",error}};
    std::cout<<output.dump()<<'\n';
    return error.empty()&&m.bytes>0?0:1;
}
}
int main(int argc,char** argv) {
    using namespace xf;
    try {
        std::string mode="ui",value;
        bool headless=false,exercise=false; double seconds=0;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--headless") headless=true;
            else if(arg=="--exercise") exercise=true;
            else if(arg=="--seconds" && i+1<argc) seconds=std::stod(argv[++i]);
            else if((arg=="--probe"||arg=="--integration"||arg=="--integration-play"||arg=="--play"||arg=="--artwork") && i+1<argc) {mode=arg.substr(2);value=argv[++i];}
            else if(arg=="--help") {
                std::cout<<"XenonFin desktop client\n  xenonfin [--headless] [--seconds N]\n  xenonfin --probe URL\n  xenonfin --integration URL\n  xenonfin --integration-play URL [--headless] [--seconds N]\n  xenonfin --play FILE_OR_URL [--headless] [--seconds N] [--exercise]\n  xenonfin --artwork URL [--headless]\n\nIntegration reads JELLYFIN_USERNAME / JELLYFIN_PASSWORD, or JELLYFIN_ACCESS_TOKEN / JELLYFIN_USER_ID, from the environment. No secret values are printed.\n";return 0;
            } else throw Error("Invalid argument; use --help");
        }
        if(mode=="probe" || mode=="integration" || mode=="integration-play") {
            Http http;auto settings=credentials(value);if(mode=="probe")settings.logout();Jellyfin api(http,settings);
            auto server=api.connect();
            if(mode=="probe") {std::cout<<Json({{"server_name",server},{"connected",true}}).dump()<<'\n';return 0;}
            const char* user=std::getenv("JELLYFIN_USERNAME"),*password=std::getenv("JELLYFIN_PASSWORD");
            if(!settings.token.empty()&&!settings.user_id.empty()) api.validate_session();
            else if(user&&password) api.login(user,password);
            else {std::cout<<Json({{"connected",true},{"authenticated",false},{"reason","Supply credentials securely in environment settings"}}).dump()<<'\n';return 2;}
            auto views=api.views(),items=api.items("",0,"SortName","Movie,Audio");
            auto results=api.search(items.items.empty()?"a":items.items.front().name);
            bool negotiated=false;
            if(mode=="integration-play") {
                std::string id;
                if(const char* p=std::getenv("JELLYFIN_TEST_ITEM_ID")) id=p;
                else if(!items.items.empty()) id=items.items.front().id;
                if(id.empty()) throw Error("No media available for integration playback");
                auto item=api.detail(id);auto playback=api.negotiate(item);
                double last_report=-1,final_position=0;
                auto report=[&](double position){if(last_report<0){api.report(playback,"Playing",static_cast<int64_t>(position*10000000),false);last_report=position;}
                    else if(position-last_report>=1){api.report(playback,"Playing/Progress",static_cast<int64_t>(position*10000000),false);last_report=position;}};
                int result=play(playback.url,api.headers(false),headless,seconds,exercise,playback.start_ticks/10000000.0,playback.method=="Transcode",report,&final_position,playback.method);
                api.report(playback,"Playing/Stopped",static_cast<int64_t>(final_position*10000000),false);api.stop_transcoding(playback);return result;
            }
            if(!items.items.empty()) {auto item=api.detail(items.items.front().id);auto p=api.negotiate(item);negotiated=!p.url.empty();api.stop_transcoding(p);}
            std::cout<<Json({{"connected",true},{"authenticated",true},{"libraries",views.total},{"media_items",items.total},{"search_matches",results.total},{"playback_negotiated",negotiated}}).dump()<<'\n';return 0;
        }
        if(mode=="play") return play(value,{},headless,seconds,exercise);
        if(mode=="artwork") {
            Graphics graphics(headless); Images images;auto began=SDL_GetTicks();
            while((SDL_GetTicks()-began)/1000.0<(seconds>0?seconds:5)) {
                auto image=images.get(value,{});
                if(image) {
                    for(int i=0;i<3;++i){graphics.begin();graphics.image(image,0,0,240,360);graphics.present();}
                    std::cout<<Json({{"width",image->width},{"height",image->height},{"rgba_bytes",image->rgba.size()}}).dump()<<'\n';return 0;
                }
                SDL_Delay(10);
            }
            throw Error("Artwork could not be downloaded or decoded");
        }
        Application app(headless); return app.run(seconds);
    } catch(const Error& e) {std::cerr<<e.what()<<'\n';return 1;}
      catch(const std::exception&) {std::cerr<<"Invalid input or resource failure\n";return 1;}
}
