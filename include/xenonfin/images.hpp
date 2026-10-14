#pragma once
#include "network.hpp"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace xf {
struct Image { int width=0,height=0; std::vector<uint8_t> rgba; };
class Images {
    struct Entry { std::shared_ptr<Image> image; uint64_t used=0; bool requested=false; };
    std::map<std::string,Entry> cache_;
    std::deque<std::pair<std::string,Headers>> jobs_;
    std::mutex mutex_;
    std::condition_variable cv_;
    Cancel cancel_=std::make_shared<std::atomic<bool>>(false);
    bool stop_=false;
    uint64_t clock_=0;
    size_t bytes_=0;
    std::thread thread_;
    void run();
public:
    Images();
    ~Images();
    std::shared_ptr<Image> get(const std::string& url,const Headers&);
};
}
