#pragma once
#include "graphics.hpp"
#include "input.hpp"
#include "jellyfin.hpp"
#include <future>
#include <functional>

namespace xf {
class Reporter {
    struct Event { Settings settings; Playback playback; std::string type; int64_t ticks; bool paused; };
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Event> events_;
    bool stop_=false;
    std::string error_;
    std::thread thread_;
    void run();
public:
    Reporter();
    ~Reporter();
    void send(const Settings&,const Playback&,const std::string&,double,bool);
    std::string error();
};
class Application {
    friend class UiFlow;
    struct View {
        std::string title="Home",parent,types,search,sort="SortName";
        int start=0,selected=0;
        Page page;
    };
    struct Result { Settings settings; Page page; Item item; Playback playback; std::string message; };
    enum class Screen { Keyboard,Library,Details,Settings,Player };
    enum class Purpose { Server,Username,Password,Search };
    enum class Job { Connect,Validate,Login,Page,Detail,Play,Album };
    Graphics graphics_;
    Input input_;
    Settings settings_;
    std::string settings_path_,server_name_,username_,error_,notice_;
    std::unique_ptr<Images> images_;
    Screen screen_=Screen::Library,keyboard_return_=Screen::Library;
    Purpose purpose_=Purpose::Server;
    Keyboard keyboard_;
    View view_;
    std::vector<View> history_;
    Item detail_;
    std::vector<Item> queue_;
    size_t queue_index_=0;
    bool shuffle_=false,controls_=true,playing_=false,quit_=false;
    int repeat_=0,tab_=0,setting_selected_=0,detail_selected_=0,music_section_=0;
    Playback playback_;
    Player player_;
    VideoFrame frame_;
    Reporter reporter_;
    double last_position_=0;
    uint32_t last_report_=0;
    Cancel cancel_;
    std::future<Result> future_;
    bool busy_=false;
    Job job_=Job::Page;
    void submit(Job,std::function<Result(Jellyfin&,Settings&,Cancel)>);
    void complete();
    void page();
    void section(int);
    void keyboard(Purpose,const std::string&,const std::string& initial="",bool masked=false);
    void keyboard_done();
    void select_item(bool info=false);
    void play_item(Item);
    void finish_playback(bool natural=false);
    void next_track(int direction);
    void action(Action);
    void render();
    void save();
public:
    explicit Application(bool headless=false);
    ~Application();
    int run(double maximum_seconds=0);
};
}
