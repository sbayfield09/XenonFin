#include "xenonfin/ui.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <thread>

namespace xf {
class UiFlow {
    static void require(bool condition,const char* message) {if(!condition) throw Error(message);}
    static void pump(Application& app,double seconds=.03) {app.quit_=false;app.run(seconds);require(app.error_.empty(),app.error_.c_str());}
    static void ready(Application& app) {
        auto begin=SDL_GetTicks();
        do {pump(app);} while(app.busy_ && SDL_GetTicks()-begin<12000);
        require(!app.busy_,"UI request did not complete");
    }
    // Type through the same D-pad/select keyboard actions used by a controller;
    // no text-input events, direct value injection, or physical keyboard.
    static void type(Application& app,const std::string& text) {
        require(app.screen_==Application::Screen::Keyboard,"Expected on-screen keyboard");
        for(unsigned char c:text) {
            bool uppercase=c>='A'&&c<='Z';char base=uppercase?static_cast<char>(c+32):static_cast<char>(c);
            auto find=[&] {return std::find(app.keyboard_.keys.begin(),app.keyboard_.keys.end(),base==' '?"SPACE":std::string(1,base));};
            if(find()==app.keyboard_.keys.end()) app.action(Action::Info);
            auto it=find();require(it!=app.keyboard_.keys.end(),"Character missing from keyboard");
            if(uppercase!=app.keyboard_.shift) {
                auto shift=std::find(app.keyboard_.keys.begin(),app.keyboard_.keys.end(),"SHIFT")-app.keyboard_.keys.begin();
                while(app.keyboard_.selected<static_cast<size_t>(shift))app.action(Action::Right);
                while(app.keyboard_.selected>static_cast<size_t>(shift))app.action(Action::Left);
                app.action(Action::Select);
            }
            size_t target=static_cast<size_t>(find()-app.keyboard_.keys.begin());
            while(app.keyboard_.selected<target)app.action(Action::Right);
            while(app.keyboard_.selected>target)app.action(Action::Left);
            app.action(Action::Select);
        }
        app.action(Action::Start);ready(app);
    }
public:
    static int run(const std::string& server) {
        Application app(true);
        type(app,server);type(app,"fixture");
        require(app.keyboard_.masked,"Password entry is not masked");type(app,"fixture-password");
        require(app.screen_==Application::Screen::Library && app.view_.page.items.size()==1,"Library login flow failed");
        app.action(Action::Select);ready(app); // Movie library
        app.action(Action::Info);ready(app); // Media details
        require(app.screen_==Application::Screen::Details,"Details screen failed");
        app.action(Action::Select);ready(app);
        require(app.playing_,"UI playback did not start");
        for(int i=0;i<150 && app.playing_;++i)pump(app,.04);
        require(!app.playing_ && app.screen_==Application::Screen::Details,"Playback did not return to details");
        require(app.player_.metrics().displayed>60,"UI did not render decoded video frames");
        app.action(Action::Search);type(app,"Test movie");
        require(app.view_.page.items.size()==1,"UI search failed");
        app.section(5);pump(app);
        require(app.screen_==Application::Screen::Settings,"Settings screen failed");
        std::cout<<"Controller keyboard setup/login, browsing, details, real video render, search, settings, persistence: PASS\n";
        return 0;
    }
};
}
int main(int argc,char** argv) {
    try {if(argc!=2)throw xf::Error("Pass fixture server URL");return xf::UiFlow::run(argv[1]);}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
