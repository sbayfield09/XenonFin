#pragma once
#include "network.hpp"
#include <SDL.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace xf {
struct VideoFrame {
    int width=0,height=0;
    double pts=0;
    std::vector<uint8_t> pixels;
};
struct Metrics {
    uint64_t decoded=0,displayed=0,dropped=0,underruns=0,bytes=0,audio_samples=0;
    double seconds=0,max_av_drift=0;
    std::string video_codec,audio_codec,container;
    int width=0,height=0;
};
class Player {
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<VideoFrame> frames_;
    std::string error_;
    Metrics metrics_;
    std::atomic<bool> stop_{false},paused_{false},finished_{true},buffering_{false};
    std::atomic<double> seek_{-1},audio_end_{0},anchor_{0},offset_{0},duration_{0},volume_{1};
    std::atomic<SDL_AudioDeviceID> audio_{0};
    std::atomic<bool> audio_seen_{false};
    double paused_clock_=0;
    void run(std::string,Headers,double,bool);
public:
    ~Player();
    void open(const std::string&,const Headers& = {},double start=0,bool transcoded=false);
    void stop();
    void pause();
    void seek(double seconds);
    void volume(double value);
    double volume() const { return volume_.load(); }
    double position() const;
    double duration() const { return duration_.load(); }
    bool paused() const { return paused_.load(); }
    bool finished() const { return finished_.load(); }
    bool buffering() const { auto d=audio_.load();return buffering_.load() || (!paused_ && !finished_ && d && audio_seen_ && SDL_GetQueuedAudioSize(d)==0); }
    bool next(VideoFrame&);
    std::string error() const;
    Metrics metrics() const;
};
}
