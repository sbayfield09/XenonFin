#include "xenonfin/images.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cstring>

namespace xf {
static std::shared_ptr<Image> decode_image(const std::string& bytes) {
    if(bytes.size()<8) throw Error("Invalid artwork");
    AVCodecID id=bytes.compare(1,3,"PNG")==0 ? AV_CODEC_ID_PNG : AV_CODEC_ID_MJPEG;
    const AVCodec* codec=avcodec_find_decoder(id);
    AVCodecContext* context=avcodec_alloc_context3(codec);
    AVFrame* frame=av_frame_alloc();
    AVPacket* packet=av_packet_alloc();
    SwsContext* scaler=nullptr;
    auto cleanup=[&] { sws_freeContext(scaler); av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&context); };
    try {
        if(!context || !frame || !packet) throw Error("Artwork allocation failed");
        context->max_pixels=2048*2048;
        if(avcodec_open2(context,codec,nullptr)<0 || av_new_packet(packet,static_cast<int>(bytes.size()))<0) throw Error("Artwork decoder failed");
        std::memcpy(packet->data,bytes.data(),bytes.size());
        if(avcodec_send_packet(context,packet)<0 || avcodec_receive_frame(context,frame)<0 || frame->width<1 || frame->height<1 || frame->width>2048 || frame->height>2048)
            throw Error("Invalid artwork dimensions");
        auto image=std::make_shared<Image>();
        double ratio=std::min({1.0,240.0/frame->width,360.0/frame->height});
        image->width=std::max(1,static_cast<int>(frame->width*ratio)); image->height=std::max(1,static_cast<int>(frame->height*ratio));
        image->rgba.resize(image->width*image->height*4);
        scaler=sws_getContext(frame->width,frame->height,static_cast<AVPixelFormat>(frame->format),image->width,image->height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!scaler) throw Error("Artwork resize failed");
        uint8_t* output[]={image->rgba.data(),nullptr,nullptr,nullptr}; int strides[]={image->width*4,0,0,0};
        sws_scale(scaler,frame->data,frame->linesize,0,frame->height,output,strides);
        cleanup(); return image;
    } catch(...) { cleanup(); throw; }
}
Images::Images() : thread_(&Images::run,this) {}
Images::~Images() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_=true; cancel_->store(true); }
    cv_.notify_all(); thread_.join();
}
std::shared_ptr<Image> Images::get(const std::string& url,const Headers& headers) {
    if(url.empty()) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry=cache_[url]; entry.used=++clock_;
    if(!entry.requested && jobs_.size()<12) { entry.requested=true; jobs_.emplace_back(url,headers); cv_.notify_one(); }
    return entry.image;
}
void Images::run() {
    Http http;
    for(;;) {
        std::pair<std::string,Headers> job;
        { std::unique_lock<std::mutex> lock(mutex_); cv_.wait(lock,[&]{return stop_ || !jobs_.empty();});
          if(stop_) return;
          job=std::move(jobs_.front()); jobs_.pop_front(); }
        std::shared_ptr<Image> image;
        try {
            Request request; request.url=job.first; request.headers=job.second; request.max_bytes=2*1024*1024; request.cancel=cancel_;
            auto response=http.request(request); check_response(response); image=decode_image(response.body);
        } catch(const std::exception&) { /* Optional artwork failure never exposes tokens or blocks browsing. */ }
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entry=cache_[job.first]; entry.image=image;
        if(image) bytes_+=image->rgba.size();
        while(bytes_>16*1024*1024 || cache_.size()>80) {
            auto oldest=cache_.end();
            for(auto it=cache_.begin();it!=cache_.end();++it)
                if(it->first!=job.first && (oldest==cache_.end() || it->second.used<oldest->second.used)) oldest=it;
            if(oldest==cache_.end()) break;
            if(oldest->second.image) bytes_-=oldest->second.image->rgba.size();
            cache_.erase(oldest);
        }
    }
}
}
