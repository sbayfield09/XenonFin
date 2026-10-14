#include "xenonfin/ui.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace xf {
static const SDL_Color accent{102,97,245,255},muted{157,171,194,255};
static std::string time_text(double seconds) {
    int n=std::max(0,static_cast<int>(seconds));
    return std::to_string(n/60)+":"+(n%60<10?"0":"")+std::to_string(n%60);
}
Reporter::Reporter() : thread_(&Reporter::run,this) {}
Reporter::~Reporter() { { std::lock_guard<std::mutex> lock(mutex_); stop_=true; } cv_.notify_all(); thread_.join(); }
void Reporter::send(const Settings& settings,const Playback& playback,const std::string& type,double seconds,bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(type=="Playing/Progress") {
        for(auto it=events_.begin();it!=events_.end();) {
            if(it->type==type && it->playback.session_id==playback.session_id) it=events_.erase(it); else ++it;
        }
    }
    if(events_.size()>=32) { error_="Playback reporting queue is full; server progress may be delayed"; return; }
    events_.push_back({settings,playback,type,static_cast<int64_t>(std::max(0.0,seconds)*10000000),paused}); cv_.notify_one();
}
std::string Reporter::error() { std::lock_guard<std::mutex> lock(mutex_); return error_; }
void Reporter::run() {
    Http http;
    for(;;) {
        Event e;
        { std::unique_lock<std::mutex> lock(mutex_); cv_.wait(lock,[&]{return stop_ || !events_.empty();});
          if(events_.empty() && stop_) return;
          e=std::move(events_.front()); events_.pop_front(); }
        try { Jellyfin api(http,e.settings); api.report(e.playback,e.type,e.ticks,e.paused);
              if(e.type=="Playing/Stopped") api.stop_transcoding(e.playback);
              std::lock_guard<std::mutex> lock(mutex_); error_.clear(); }
        catch(const std::exception&) { std::lock_guard<std::mutex> lock(mutex_); error_="Server playback reporting failed; resume position may be stale"; }
    }
}
Application::Application(bool headless) : graphics_(headless),settings_(Settings::load(Settings::default_path())),settings_path_(Settings::default_path()),images_(std::make_unique<Images>()) {
    if(!settings_.token.empty()) submit(Job::Validate,[](Jellyfin& api,Settings&,Cancel c){api.validate_session(c);return Result{};});
    else keyboard(Purpose::Server,"Connect to your Jellyfin server",settings_.server);
}
Application::~Application() {
    if(cancel_) cancel_->store(true);
    if(future_.valid()) future_.wait();
    if(playing_) finish_playback();
    player_.stop();
}
void Application::save() {
    try { settings_.save(settings_path_); }
    catch(const std::exception& e) { error_=e.what(); }
}
void Application::submit(Job job,std::function<Result(Jellyfin&,Settings&,Cancel)> task) {
    if(busy_) return;
    job_=job; busy_=true; error_.clear(); cancel_=std::make_shared<std::atomic<bool>>(false);
    auto copy=settings_; auto cancel=cancel_;
    future_=std::async(std::launch::async,[copy,cancel,task]() mutable {
        Http http; Jellyfin api(http,copy);
        auto result=task(api,copy,cancel); result.settings=copy; return result;
    });
}
void Application::complete() {
    if(!busy_ || future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    busy_=false;
    try {
        auto r=future_.get();
        if(cancel_->load()) { notice_="Request cancelled"; return; }
        settings_=std::move(r.settings);
        switch(job_) {
            case Job::Connect: server_name_=r.message; save(); keyboard(Purpose::Username,"Jellyfin username",settings_.user_name); break;
            case Job::Validate: case Job::Login: save(); section(0); break;
            case Job::Page:
                view_.page=std::move(r.page); view_.selected=0;
                if(!view_.search.empty()) std::stable_sort(view_.page.items.begin(),view_.page.items.end(),[](const Item& a,const Item& b){return a.type<b.type;});
                screen_=Screen::Library; break;
            case Job::Detail: detail_=r.item; detail_selected_=0; screen_=Screen::Details; break;
            case Job::Album:
                if(r.page.items.empty()) throw Error("Album has no accessible tracks");
                queue_=std::move(r.page.items); queue_index_=0;
                if(shuffle_) std::shuffle(queue_.begin(),queue_.end(),std::mt19937(std::random_device{}()));
                play_item(queue_[0]); break;
            case Job::Play:
                playback_=r.playback;
                { Http http; Jellyfin api(http,settings_); player_.open(playback_.url,api.headers(false),playback_.start_ticks/10000000.0,playback_.method=="Transcode"); }
                screen_=Screen::Player; playing_=true; frame_={}; last_position_=playback_.start_ticks/10000000.0; last_report_=SDL_GetTicks();
                // Report start only after decoder initialization has succeeded (in run).
                notice_=""; break;
        }
    } catch(const Error& e) {
        error_=e.what();
        if(e.status==401 || e.status==403) { settings_.logout(); save(); keyboard(Purpose::Username,"Jellyfin username"); }
        else if(job_==Job::Connect) keyboard(Purpose::Server,"Connect to your Jellyfin server",settings_.server);
        else if(job_==Job::Login) keyboard(Purpose::Username,"Jellyfin username",username_);
    } catch(const std::exception&) { error_="Malformed server response or local resource failure"; }
}
void Application::page() {
    auto v=view_;
    submit(Job::Page,[v](Jellyfin& api,Settings&,Cancel c) {
        Result r;
        if(!v.search.empty()) r.page=api.search(v.search,v.start,c);
        else if(v.parent.empty() && v.types.empty()) r.page=api.views(c);
        else r.page=api.items(v.parent,v.start,v.sort,v.types,c);
        return r;
    });
}
void Application::section(int index) {
    if(settings_.token.empty()) { keyboard(Purpose::Username,"Jellyfin username"); return; }
    tab_=(index+6)%6; history_.clear(); view_={};
    switch(tab_) {
        case 0:view_.title="Your libraries";break;
        case 1:view_.title="Movies";view_.types="Movie";break;
        case 2:view_.title="TV shows";view_.types="Series";break;
        case 3:view_.title="Music albums";view_.types="MusicAlbum";break;
        case 4:keyboard(Purpose::Search,"Search your Jellyfin library");return;
        case 5:screen_=Screen::Settings;setting_selected_=0;return;
    }
    screen_=Screen::Library; page();
}
void Application::keyboard(Purpose purpose,const std::string& title,const std::string& initial,bool masked) {
    keyboard_return_=screen_==Screen::Keyboard?Screen::Library:screen_;
    purpose_=purpose; keyboard_=Keyboard(title,initial,masked); screen_=Screen::Keyboard;
}
void Application::keyboard_done() {
    std::string value=keyboard_.value;
    keyboard_.value.assign(keyboard_.value.size(),'\0'); keyboard_.value.clear();
    screen_=keyboard_return_;
    if(purpose_==Purpose::Server) {
        try { auto url=Url::parse(value); if(url.base!=settings_.server) { settings_.logout(); queue_.clear(); images_=std::make_unique<Images>(); } settings_.server=url.base; }
        catch(const std::exception& e) { error_=e.what(); keyboard(Purpose::Server,"Connect to your Jellyfin server",value); return; }
        submit(Job::Connect,[](Jellyfin& api,Settings&,Cancel c){Result r;r.message=api.connect(c);return r;});
    } else if(purpose_==Purpose::Username) { username_=value; keyboard(Purpose::Password,"Jellyfin password","",true); }
    else if(purpose_==Purpose::Password) {
        auto username=username_;
        submit(Job::Login,[username,password=std::move(value)](Jellyfin& api,Settings&,Cancel c) mutable {
            api.login(username,password,c); std::fill(password.begin(),password.end(),'\0'); password.clear(); return Result{};
        });
    } else if(purpose_==Purpose::Search) {
        view_={};view_.title="Search: "+value;view_.search=value; screen_=Screen::Library;
        if(value.empty()) {view_.page={};return;} page();
    }
}
void Application::select_item(bool info) {
    if(view_.page.items.empty()) return;
    auto item=view_.page.items[view_.selected];
    if(item.folder && !info) {
        if(history_.size()>=16) history_.erase(history_.begin());
        history_.push_back(view_);view_={};view_.title=item.name;view_.parent=item.id; page();
    } else submit(Job::Detail,[id=item.id](Jellyfin& api,Settings&,Cancel c){Result r;r.item=api.detail(id,c);return r;});
}
void Application::play_item(Item item) {
    if(playing_) finish_playback();
    detail_=item;
    submit(Job::Play,[item](Jellyfin& api,Settings&,Cancel c){Result r;r.playback=api.negotiate(item,c);return r;});
}
void Application::finish_playback(bool natural) {
    if(!playing_) return;
    last_position_=std::min(player_.duration()>0?player_.duration():last_position_,last_position_);
    player_.stop(); playing_=false;
    detail_.resume_ticks=static_cast<int64_t>(last_position_*10000000);
    reporter_.send(settings_,playback_,"Playing/Stopped",last_position_,false);
    screen_=Screen::Details;
    if(natural && !queue_.empty()) {
        if(repeat_==2) {auto item=queue_[queue_index_];item.resume_ticks=0;play_item(item);}
        else next_track(1);
    }
}
void Application::next_track(int direction) {
    if(queue_.empty()) return;
    int target=static_cast<int>(queue_index_)+direction;
    if(target<0 || target>=static_cast<int>(queue_.size())) {
        if(repeat_!=1) return;
        target=(target+static_cast<int>(queue_.size()))%static_cast<int>(queue_.size());
    }
    queue_index_=target; auto item=queue_[queue_index_]; item.resume_ticks=0; play_item(item);
}
void Application::action(Action a) {
    if(a==Action::Quit) {quit_=true;return;}
    if(busy_) { if(a==Action::Back) cancel_->store(true); return; }
    if(!error_.empty() && a==Action::Back) { error_.clear(); return; }
    if(screen_==Screen::Keyboard) {
        if(a==Action::Search) a=Action::Delete;
        keyboard_.input(a);
        if(keyboard_.done) keyboard_done();
        else if(keyboard_.cancelled) { keyboard_.value.assign(keyboard_.value.size(),'\0');keyboard_.value.clear();screen_=keyboard_return_; }
        return;
    }
    if(screen_==Screen::Player) {
        if(a==Action::Select) {player_.pause();reporter_.send(settings_,playback_,"Playing/Progress",player_.position(),player_.paused());}
        if(a==Action::Back) finish_playback();
        if(a==Action::Start) controls_=!controls_;
        if(a==Action::Previous) next_track(-1);
        if(a==Action::Next) next_track(1);
        if(a==Action::Up) player_.volume(player_.volume()+0.1);
        if(a==Action::Down) player_.volume(player_.volume()-0.1);
        if(a==Action::SeekBack || a==Action::SeekForward) {
            double target=std::max(0.0,player_.position()+(a==Action::SeekBack?-10:10));
            if(player_.duration()>0) target=std::min(target,std::max(0.0,player_.duration()-0.5));
            if(playback_.method=="Transcode") { auto item=detail_; item.resume_ticks=static_cast<int64_t>(target*10000000); play_item(item); }
            else player_.seek(target);
        }
        return;
    }
    if(a==Action::Search) {keyboard(Purpose::Search,"Search your Jellyfin library");return;}
    if(a==Action::Previous||a==Action::Next) {section(tab_+(a==Action::Next?1:-1));return;}
    if(screen_==Screen::Settings) {
        if(a==Action::Up) setting_selected_=std::max(0,setting_selected_-1);
        if(a==Action::Down) setting_selected_=std::min(7,setting_selected_+1);
        if(a==Action::Back) section(0);
        if(a==Action::Select) switch(setting_selected_) {
            case 0:keyboard(Purpose::Server,"Edit Jellyfin server",settings_.server);break;
            case 1:settings_.logout();queue_.clear();images_=std::make_unique<Images>();save();keyboard(Purpose::Username,"Jellyfin username");break;
            case 2:settings_.remember_login=!settings_.remember_login;save();break;
            case 3:settings_.video_bitrate=settings_.video_bitrate>=5000000?1000000:settings_.video_bitrate+1000000;save();break;
            case 4:
                shuffle_=!shuffle_;
                if(shuffle_ && !queue_.empty()) {
                    auto id=queue_[queue_index_].id;std::shuffle(queue_.begin(),queue_.end(),std::mt19937(std::random_device{}()));
                    queue_index_=std::find_if(queue_.begin(),queue_.end(),[&](const Item& item){return item.id==id;})-queue_.begin();
                }
                break;
            case 5:repeat_=(repeat_+1)%3;break;
            case 6:queue_.clear();queue_index_=0;notice_="Music queue cleared";break;
            case 7:quit_=true;break;
        }
        return;
    }
    if(screen_==Screen::Details) {
        if(a==Action::Back) screen_=Screen::Library;
        if(a==Action::Start) {screen_=Screen::Settings;return;}
        if(a==Action::Down) detail_selected_=std::min(2,detail_selected_+1);
        if(a==Action::Up) detail_selected_=std::max(0,detail_selected_-1);
        if(a==Action::Select) {
            if(detail_.folder && detail_.type!="MusicAlbum") {
                history_.push_back(view_);view_={};view_.title=detail_.name;view_.parent=detail_.id;screen_=Screen::Library;page();return;
            }
            if(detail_.folder || detail_.type=="MusicAlbum") {
                auto id=detail_.id;
                submit(Job::Album,[id](Jellyfin& api,Settings&,Cancel c) {
                    Result r;
                    for(int start=0;start<200;start+=40) {
                        auto p=api.items(id,start,"ParentIndexNumber,IndexNumber","Audio",c);
                        r.page.items.insert(r.page.items.end(),p.items.begin(),p.items.end());
                        if(start+40>=p.total) return r;
                    }
                    throw Error("Album exceeds the 200-track queue limit; browse and queue individual tracks");
                });
            } else if(detail_selected_==1 && detail_.type=="Audio") {
                if(queue_.size()>=200) {error_="Music queue is full (200 tracks)";return;}
                queue_.push_back(detail_);notice_="Track added to queue";
            } else {
                auto item=detail_;
                if(detail_selected_==2) item.resume_ticks=0;
                if(item.type=="Audio") {
                    auto it=std::find_if(queue_.begin(),queue_.end(),[&](const Item& track){return track.id==item.id;});
                    if(it==queue_.end()) { if(queue_.size()>=200) {error_="Music queue is full";return;} queue_.push_back(item);queue_index_=queue_.size()-1; }
                    else queue_index_=static_cast<size_t>(it-queue_.begin());
                }
                else queue_.clear();
                play_item(item);
            }
        }
        return;
    }
    if(screen_==Screen::Library) {
        auto count=static_cast<int>(view_.page.items.size());
        if(a==Action::Left && view_.selected>0) --view_.selected;
        else if(a==Action::Left && view_.start>=40) {view_.start-=40;page();}
        if(a==Action::Right && view_.selected+1<count) ++view_.selected;
        else if(a==Action::Right && view_.start+count<view_.page.total) {view_.start+=40;page();}
        if(a==Action::Down) view_.selected=std::min(std::max(0,count-1),view_.selected+5);
        if(a==Action::Up) view_.selected=std::max(0,view_.selected-5);
        if(a==Action::Select) select_item();
        if(a==Action::Info) select_item(true);
        if(a==Action::Start) {
            const std::vector<std::string> sorts={"SortName","DateCreated","PremiereDate","DatePlayed"};
            auto it=std::find(sorts.begin(),sorts.end(),view_.sort);
            view_.sort=sorts[(it-sorts.begin()+1)%sorts.size()];view_.start=0;page();
        }
        if(a==Action::Back) {
            if(!history_.empty()) {view_=std::move(history_.back());history_.pop_back();}
            else section(5);
        }
        if(a==Action::Delete && tab_==3) {
            music_section_=(music_section_+1)%3;
            view_={};view_.types=music_section_==0?"MusicAlbum":music_section_==1?"MusicArtist":"Audio";view_.title=music_section_==0?"Music albums":music_section_==1?"Music artists":"Music tracks";page();
        }
    }
}
void Application::render() {
    graphics_.begin();
    if(screen_==Screen::Player) {
        if(player_.next(frame_)) {}
        graphics_.video(frame_);
        if(playback_.audio && !busy_) {
            Http http;Jellyfin api(http,settings_);auto cover=images_->get(api.artwork_url(detail_),api.headers(false));
            graphics_.rect(430,150,420,340,{30,37,55,255});graphics_.image(cover,430,150,420,340);
            graphics_.text(detail_.artist+"  /  "+detail_.album,48,500,22,muted);
        }
        if(controls_ || player_.buffering() || player_.paused()) {
            graphics_.rect(0,550,1280,170,{12,16,28,230});
            graphics_.text(detail_.name,48,570,28);graphics_.text(player_.paused()?"Paused":"Playing",48,610,20,accent);
            double duration=player_.duration();
            graphics_.rect(250,620,740,5,{60,66,86,255});
            graphics_.rect(250,620,duration>0?static_cast<int>(740*std::clamp(last_position_/duration,0.0,1.0)):0,5,accent);
            graphics_.text(time_text(last_position_)+" / "+time_text(duration),1020,605,20);
            graphics_.text("A Pause   B Stop   LT/RT Seek   LB/RB Track   Up/Down Volume   Start Controls",48,665,19,muted);
            if(player_.buffering()) graphics_.text("Buffering...",510,330,32,accent);
        }
    } else {
        graphics_.text("XENONFIN",48,28,30,accent);
        graphics_.text(settings_.user_name.empty()?"Your Jellyfin library":settings_.user_name+"  /  "+server_name_,900,36,18,muted,330);
        const char* tabs[]={"Home","Movies","TV","Music","Search","Settings"};
        for(int i=0;i<6;++i) {graphics_.text(tabs[i],48+i*158,88,22,i==tab_?accent:muted);if(i==tab_)graphics_.rect(48+i*158,122,100,3,accent);}
        if(screen_==Screen::Keyboard) {
            graphics_.text(keyboard_.title,80,160,30);
            graphics_.rect(80,220,1120,66,{30,37,55,255});graphics_.text(keyboard_.display()+"_",98,230,26,{230,234,244,255},1080);
            for(size_t i=0;i<keyboard_.keys.size();++i) {
                int x=80+static_cast<int>(i%10)*112,y=320+static_cast<int>(i/10)*49;
                graphics_.rect(x,y,105,43,i==keyboard_.selected?accent:SDL_Color{30,37,55,255});
                auto label=keyboard_.keys[i];if(keyboard_.shift && label.size()==1 && label[0]>='a' && label[0]<='z')label[0]-=32;
                graphics_.text(label,x+12,y+4,20);
            }
            graphics_.text("D-pad Move   A Type   X Delete   Y Symbols   B Cancel   Start Done",80,642,20,muted);
        } else if(screen_==Screen::Library) {
            graphics_.text(view_.title,48,155,34);
            graphics_.text(std::to_string(view_.start+view_.page.items.size())+" / "+std::to_string(view_.page.total)+" items   Sort: "+view_.sort,48,203,18,muted);
            int first=std::max(0,view_.selected-2);
            first=std::min(first,std::max(0,static_cast<int>(view_.page.items.size())-5));
            for(int col=0;col<5 && first+col<static_cast<int>(view_.page.items.size());++col) {
                int index=first+col,x=48+col*239;const auto& item=view_.page.items[index];
                graphics_.rect(x-4,258,223,331,index==view_.selected?accent:SDL_Color{30,37,55,255});
                graphics_.rect(x,262,215,254,{23,29,45,255});
                if(!busy_) {Http http;Jellyfin api(http,settings_);auto image=images_->get(api.artwork_url(item),api.headers(false));if(image)graphics_.image(image,x,262,215,254);}
                graphics_.text(item.type,x+12,365,17,muted,188);
                graphics_.text(item.name,x+8,530,20,{230,234,244,255},200);
                graphics_.text(item.year?std::to_string(item.year):item.album,x+8,560,16,muted,200);
                if(item.duration_ticks>0 && item.resume_ticks>0) {
                    graphics_.rect(x+8,582,199,3,{60,66,86,255});
                    graphics_.rect(x+8,582,static_cast<int>(199*std::clamp(item.resume_ticks/static_cast<double>(item.duration_ticks),0.0,1.0)),3,{205,210,255,255});
                }
            }
            if(view_.page.items.empty()&&!busy_) graphics_.text("No items found",48,320,26,muted);
            graphics_.text("A Open   B Back   X Search   Y Details   Start Sort   LB/RB Section",48,645,19,muted);
            if(tab_==3) graphics_.text("Select music view: "+view_.types+" (keyboard Backspace; controller Back)",48,610,17,muted);
        } else if(screen_==Screen::Details) {
            graphics_.rect(48,170,290,390,{30,37,55,255});
            if(!busy_) {Http http;Jellyfin api(http,settings_);auto image=images_->get(api.artwork_url(detail_,290),api.headers(false));if(image)graphics_.image(image,48,170,290,390);}
            graphics_.text(detail_.name,380,168,34,{230,234,244,255},820);
            graphics_.text(detail_.type+"   "+(detail_.year?std::to_string(detail_.year):"")+"   "+time_text(detail_.duration_ticks/10000000.0),380,223,21,muted);
            graphics_.text(detail_.artist+"  "+detail_.album,380,263,21,muted);
            std::string genres;for(const auto& genre:detail_.genres){if(!genres.empty())genres+=" / ";genres+=genre;if(genres.size()>500)break;}
            graphics_.text(genres,380,286,17,muted,800);
            graphics_.wrapped(detail_.overview,380,308,800,5);
            for(int i=0;i<3;++i) {
                std::string labels[]={detail_.folder?(detail_.type=="MusicAlbum"?"Play album":"Open"):detail_.resume_ticks>0?"Resume":"Play",detail_.type=="Audio"?"Add to queue":"Play","Play from beginning"};
                graphics_.rect(380,490+i*48,600,41,i==detail_selected_?accent:SDL_Color{30,37,55,255});graphics_.text(labels[i],400,496+i*48,20);
            }
            graphics_.text("A Select   B Back   X Search   Start Settings",48,660,19,muted);
        } else if(screen_==Screen::Settings) {
            graphics_.text("Settings",48,154,34);
            const std::vector<std::string> labels={"Server: "+settings_.server,"Log out","Remember login: "+std::string(settings_.remember_login?"Yes":"No"),
                "Video bitrate: "+std::to_string(settings_.video_bitrate/1000000)+" Mbps (desktop)","Shuffle: "+std::string(shuffle_?"On":"Off"),
                "Repeat: "+std::string(repeat_==0?"Off":repeat_==1?"Queue":"Track"),"Clear music queue ("+std::to_string(queue_.size())+" tracks)","Exit"};
            for(size_t i=0;i<labels.size();++i) {graphics_.rect(48,220+i*48,1160,42,static_cast<int>(i)==setting_selected_?accent:SDL_Color{30,37,55,255});graphics_.text(labels[i],68,226+i*48,21);}
        }
    }
    if(busy_) {graphics_.rect(380,312,520,80,{20,27,43,245});graphics_.text("Loading"+std::string((SDL_GetTicks()/300)%4,'.')+"  /  B Cancel",414,332,26,accent);}
    if(!notice_.empty()) graphics_.text(notice_,48,690,16,muted);
    if(!error_.empty()) {graphics_.rect(32,28,1216,56,{126,40,59,250});graphics_.text(error_,48,42,21,{255,240,245,255},1170);}
    auto report_error=reporter_.error();if(!report_error.empty())graphics_.text(report_error,48,690,16,{255,170,160,255});
    graphics_.present();
}
int Application::run(double maximum_seconds) {
    auto begin=SDL_GetTicks();bool reported_start=false;
    while(!quit_) {
        complete();
        for(auto a:input_.poll(screen_==Screen::Keyboard?&keyboard_:nullptr)) action(a);
        if(playing_) {
            last_position_=player_.position();
            if(!player_.buffering() && !reported_start && !player_.finished()) {reporter_.send(settings_,playback_,"Playing",last_position_,false);reported_start=true;}
            if(SDL_GetTicks()-last_report_>=5000 && reported_start) {reporter_.send(settings_,playback_,"Playing/Progress",last_position_,player_.paused());last_report_=SDL_GetTicks();}
            if(player_.finished()) {
                auto error=player_.error();
                finish_playback(error.empty());reported_start=false;
                if(!error.empty()) error_=error;
            }
        } else reported_start=false;
        render(); SDL_Delay(8);
        if(maximum_seconds>0 && (SDL_GetTicks()-begin)/1000.0>=maximum_seconds) quit_=true;
    }
    return 0;
}
}
