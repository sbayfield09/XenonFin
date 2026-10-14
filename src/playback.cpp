#include "xenonfin/playback.hpp"
#include <curl/curl.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>

namespace xf {
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
namespace {
// Compressed media never accumulates beyond this ring. Seek cancels and joins
// the transfer before starting a new range request; only one transfer owns it.
class Stream {
    std::array<uint8_t,2*1024*1024> data_{};
    size_t head_=0,count_=0;
    int64_t position_=0,length_=-1,range_start_=0;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool>& stop_;
    bool eof_=false,failed_=false;
    long response_status_=0;
    std::string url_;
    Headers headers_;
    static size_t header_cb(char* bytes,size_t a,size_t b,void* opaque) {
        auto& s=*static_cast<Stream*>(opaque);
        std::string line(bytes,a*b);
        if(line.rfind("HTTP/",0)==0) {
            auto space=line.find(' ');
            if(space!=std::string::npos) s.response_status_=std::strtol(line.c_str()+space+1,nullptr,10);
        }
        return a*b;
    }
    static size_t write_cb(char* bytes,size_t a,size_t b,void* opaque) {
        auto& s=*static_cast<Stream*>(opaque);
        if(s.response_status_!=200 && s.response_status_!=206) return 0;
        if(s.range_start_>0 && s.response_status_!=206) return 0;
        size_t n=a*b,at=0;
        std::unique_lock<std::mutex> lock(s.mutex_);
        while(at<n) {
            s.cv_.wait_for(lock,std::chrono::milliseconds(50),[&]{return s.count_<s.data_.size() || s.cancelled_ || s.stop_;});
            if(s.cancelled_ || s.stop_) return 0;
            size_t tail=(s.head_+s.count_)%s.data_.size();
            size_t amount=std::min({n-at,s.data_.size()-s.count_,s.data_.size()-tail});
            std::copy_n(reinterpret_cast<uint8_t*>(bytes)+at,amount,s.data_.data()+tail);
            s.count_+=amount; at+=amount; s.cv_.notify_all();
        }
        return n;
    }
    static int progress_cb(void* opaque,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
        auto& s=*static_cast<Stream*>(opaque);
        return s.cancelled_ || s.stop_;
    }
    void start(int64_t position) {
        cancelled_=false; eof_=false; failed_=false; position_=position; range_start_=position; head_=0; count_=0; response_status_=0;
        thread_=std::thread([this,position] {
            auto c=std::unique_ptr<CURL,decltype(&curl_easy_cleanup)>(curl_easy_init(),curl_easy_cleanup);
            curl_slist* raw=nullptr;
            for(const auto& h:headers_) raw=curl_slist_append(raw,h.c_str());
            auto hs=std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)>(raw,curl_slist_free_all);
            curl_easy_setopt(c.get(),CURLOPT_URL,url_.c_str());
            curl_easy_setopt(c.get(),CURLOPT_HTTPHEADER,hs.get());
            curl_easy_setopt(c.get(),CURLOPT_CONNECTTIMEOUT,8L);
            curl_easy_setopt(c.get(),CURLOPT_LOW_SPEED_LIMIT,1L);
            curl_easy_setopt(c.get(),CURLOPT_LOW_SPEED_TIME,20L);
            curl_easy_setopt(c.get(),CURLOPT_NOSIGNAL,1L);
            curl_easy_setopt(c.get(),CURLOPT_PROTOCOLS_STR,"http,https");
            curl_easy_setopt(c.get(),CURLOPT_SSL_VERIFYPEER,1L);
            curl_easy_setopt(c.get(),CURLOPT_SSL_VERIFYHOST,2L);
            curl_easy_setopt(c.get(),CURLOPT_FOLLOWLOCATION,0L);
            curl_easy_setopt(c.get(),CURLOPT_WRITEFUNCTION,write_cb);
            curl_easy_setopt(c.get(),CURLOPT_WRITEDATA,this);
            curl_easy_setopt(c.get(),CURLOPT_HEADERFUNCTION,header_cb);
            curl_easy_setopt(c.get(),CURLOPT_HEADERDATA,this);
            curl_easy_setopt(c.get(),CURLOPT_XFERINFOFUNCTION,progress_cb);
            curl_easy_setopt(c.get(),CURLOPT_XFERINFODATA,this);
            curl_easy_setopt(c.get(),CURLOPT_NOPROGRESS,0L);
            std::string range=std::to_string(position)+"-";
            if(position>0) curl_easy_setopt(c.get(),CURLOPT_RANGE,range.c_str());
            auto rc=curl_easy_perform(c.get());
            std::lock_guard<std::mutex> lock(mutex_);
            failed_=rc!=CURLE_OK; eof_=true; cv_.notify_all();
        });
    }
    void cancel() { cancelled_=true; cv_.notify_all(); if(thread_.joinable()) thread_.join(); }
public:
    Stream(std::string url,Headers headers,std::atomic<bool>& stop) : stop_(stop),url_(std::move(url)),headers_(std::move(headers)) {
        Url::parse(url_.substr(0,url_.find_first_of("/?",url_.find("://")+3)));
        for(const auto& header:headers_) if(header.find_first_of("\r\n")!=std::string::npos) throw Error("Invalid streaming header");
        Http http;
        auto cancel=std::make_shared<std::atomic<bool>>(false);
        Request r; r.url=url_; r.method="HEAD"; r.headers=headers_; r.timeout_seconds=10; r.cancel=cancel;
        try {
            auto response=http.request(r);
            if(response.status==401||response.status==403) check_response(response);
            auto it=response.headers.find("content-length");
            if(response.status==200 && it!=response.headers.end()) length_=std::stoll(it->second);
        } catch(const Error& e) { if(e.status==401||e.status==403) throw; }
        start(0);
    }
    ~Stream() { cancel(); }
    int read(uint8_t* out,int amount) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock,[&]{return count_ || eof_ || stop_ || cancelled_;});
        if(stop_||cancelled_) return AVERROR_EXIT;
        if(!count_) return failed_ ? AVERROR(EIO) : AVERROR_EOF;
        auto n=std::min(count_,static_cast<size_t>(amount));
        for(size_t i=0;i<n;++i) out[i]=data_[(head_+i)%data_.size()];
        head_=(head_+n)%data_.size(); count_-=n; position_+=n; cv_.notify_all(); return static_cast<int>(n);
    }
    int64_t seek(int64_t offset,int whence) {
        if(whence==AVSEEK_SIZE) return length_>=0?length_:AVERROR(ENOSYS);
        whence &= ~AVSEEK_FORCE;
        if(length_<0) return AVERROR(ENOSYS);
        int64_t pos=whence==SEEK_SET?offset:whence==SEEK_CUR?position_+offset:whence==SEEK_END?length_+offset:-1;
        if(pos<0 || pos>length_) return AVERROR(EINVAL);
        cancel(); start(pos); return pos;
    }
    static int read_cb(void* s,uint8_t* p,int n) { return static_cast<Stream*>(s)->read(p,n); }
    static int64_t seek_cb(void* s,int64_t n,int w) { return static_cast<Stream*>(s)->seek(n,w); }
};
struct Context {
    AVFormatContext* format=nullptr;
    AVCodecContext *video=nullptr,*audio=nullptr;
    AVFrame* frame=av_frame_alloc();
    AVPacket* packet=av_packet_alloc();
    SwsContext* scaler=nullptr;
    SwrContext* resampler=nullptr;
    AVIOContext* io=nullptr;
    std::unique_ptr<Stream> stream;
    SDL_AudioDeviceID device=0;
    ~Context() {
        if(device) SDL_CloseAudioDevice(device);
        swr_free(&resampler); sws_freeContext(scaler);
        av_frame_free(&frame); av_packet_free(&packet);
        avcodec_free_context(&video); avcodec_free_context(&audio);
        avformat_close_input(&format);
        if(io) { av_freep(&io->buffer); avio_context_free(&io); }
    }
};
AVCodecContext* decoder(AVFormatContext* format,int index) {
    auto* codec=avcodec_find_decoder(format->streams[index]->codecpar->codec_id);
    if(!codec) throw Error("No decoder for the negotiated codec");
    auto* context=avcodec_alloc_context3(codec);
    if(!context) throw Error("Decoder allocation failed");
    context->thread_count=2;
    context->max_pixels=1920*1080;
    context->max_samples=48000*8;
    if(avcodec_parameters_to_context(context,format->streams[index]->codecpar)<0 || avcodec_open2(context,codec,nullptr)<0) {
        avcodec_free_context(&context); throw Error("Decoder initialization failed");
    }
    return context;
}
}
Player::~Player() { stop(); }
void Player::open(const std::string& url,const Headers& headers,double start,bool transcoded) {
    stop();
    { std::lock_guard<std::mutex> lock(mutex_); metrics_={}; error_.clear(); frames_.clear(); }
    stop_=false; finished_=false; buffering_=true; paused_=false; audio_seen_=false;
    audio_end_=0; offset_=transcoded?start:0; duration_=0; anchor_=now(); seek_=-1;
    worker_=std::thread(&Player::run,this,url,headers,start,transcoded);
}
void Player::stop() {
    stop_=true; cv_.notify_all();
    if(worker_.joinable()) worker_.join();
    { std::lock_guard<std::mutex> lock(mutex_); frames_.clear(); }
    finished_=true; audio_=0;
}
double Player::position() const {
    if(paused_) return paused_clock_;
    auto device=audio_.load();
    double position=device && audio_seen_ ? offset_.load()+audio_end_.load()-SDL_GetQueuedAudioSize(device)/192000.0 : offset_.load()+now()-anchor_.load();
    return std::clamp(position,0.0,duration_>0?duration_.load():std::max(0.0,position));
}
void Player::pause() {
    if(paused_) { anchor_=now()-(paused_clock_-offset_); paused_=false; }
    else { paused_clock_=position(); paused_=true; }
    if(auto device=audio_.load()) SDL_PauseAudioDevice(device,paused_?1:0);
    cv_.notify_all();
}
void Player::seek(double seconds) { seek_=std::max(0.0,seconds-offset_.load()); cv_.notify_all(); }
void Player::volume(double value) { volume_=std::clamp(value,0.0,1.0); }
bool Player::next(VideoFrame& frame) {
    if(paused_) return false;
    double clock=position();
    std::lock_guard<std::mutex> lock(mutex_);
    bool updated=false;
    while(!frames_.empty() && frames_.front().pts+offset_<=clock+0.02) {
        if(updated) ++metrics_.dropped;
        frame=std::move(frames_.front()); frames_.pop_front(); updated=true;
    }
    if(updated) { ++metrics_.displayed; metrics_.max_av_drift=std::max(metrics_.max_av_drift,std::abs(frame.pts+offset_-clock)); cv_.notify_all(); }
    return updated;
}
std::string Player::error() const { std::lock_guard<std::mutex> lock(mutex_); return error_; }
Metrics Player::metrics() const { std::lock_guard<std::mutex> lock(mutex_); return metrics_; }
void Player::run(std::string url,Headers headers,double start,bool transcoded) {
    double began=now();
    try {
        av_log_set_level(AV_LOG_QUIET); // libav diagnostics can otherwise include token-bearing URLs.
        Context c;
        c.format=avformat_alloc_context();
        if(!c.format || !c.frame || !c.packet) throw Error("Playback allocation failed");
        c.format->interrupt_callback={[](void* p)->int { return static_cast<Player*>(p)->stop_.load(); },this};
        c.format->io_open=[](AVFormatContext*,AVIOContext**,const char*,int,AVDictionary**)->int { return AVERROR(EPERM); };
        bool remote=url.rfind("http://",0)==0 || url.rfind("https://",0)==0;
        if(remote) {
            c.stream=std::make_unique<Stream>(url,headers,stop_);
            auto* buf=static_cast<uint8_t*>(av_malloc(64*1024));
            c.io=avio_alloc_context(buf,64*1024,0,c.stream.get(),Stream::read_cb,nullptr,Stream::seek_cb);
            if(!c.io) { av_free(buf); throw Error("Stream allocation failed"); }
            c.format->pb=c.io; c.format->flags|=AVFMT_FLAG_CUSTOM_IO;
        } else {
            if(!std::filesystem::is_regular_file(url)) throw Error("Media file does not exist");
            // Local files are opened here rather than through io_open, which rejects nested URLs.
            if(avio_open(&c.format->pb,url.c_str(),AVIO_FLAG_READ)<0) throw Error("Cannot open media file");
        }
        AVDictionary* options=nullptr;
        av_dict_set(&options,"probesize","1048576",0);
        av_dict_set(&options,"analyzeduration","3000000",0);
        int rc=avformat_open_input(&c.format,nullptr,nullptr,&options);
        av_dict_free(&options);
        if(rc<0 || avformat_find_stream_info(c.format,nullptr)<0) throw Error("Cannot demultiplex media stream (network, format, or authorization error)");
        int vi=av_find_best_stream(c.format,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
        if(vi>=0 && (c.format->streams[vi]->disposition&AV_DISPOSITION_ATTACHED_PIC)) vi=-1;
        int ai=av_find_best_stream(c.format,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
        if(vi<0 && ai<0) throw Error("Media contains no supported audio or video");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            metrics_.container=c.format->iformat->name;
            if(vi>=0) {auto* p=c.format->streams[vi]->codecpar;metrics_.video_codec=avcodec_get_name(p->codec_id);metrics_.width=p->width;metrics_.height=p->height;}
            if(ai>=0) metrics_.audio_codec=avcodec_get_name(c.format->streams[ai]->codecpar->codec_id);
        }
        double origin=c.format->start_time==AV_NOPTS_VALUE ? 0 : c.format->start_time/static_cast<double>(AV_TIME_BASE);
        duration_=c.format->duration==AV_NOPTS_VALUE?0:c.format->duration/static_cast<double>(AV_TIME_BASE)+offset_;
        if(vi>=0) {
            c.video=decoder(c.format,vi);
            if(c.video->width>1920 || c.video->height>1080 || c.video->width<1 || c.video->height<1)
                throw Error("Video exceeds the 1080p desktop memory limit");
        }
        if(ai>=0) {
            c.audio=decoder(c.format,ai);
            SDL_AudioSpec desired{}; desired.freq=48000; desired.format=AUDIO_S16SYS; desired.channels=2; desired.samples=512;
            c.device=SDL_OpenAudioDevice(nullptr,0,&desired,nullptr,0);
            if(!c.device) throw Error("Cannot open stereo audio output");
            audio_=c.device;
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            if(swr_alloc_set_opts2(&c.resampler,&stereo,AV_SAMPLE_FMT_S16,48000,&c.audio->ch_layout,c.audio->sample_fmt,c.audio->sample_rate,0,nullptr)<0 || swr_init(c.resampler)<0)
                throw Error("Audio conversion initialization failed");
            SDL_PauseAudioDevice(c.device,0);
        }
        anchor_=now(); buffering_=false;
        if(start>0 && !transcoded) seek_=start;
        double discard_before=0,estimated_audio=0;
        auto receive=[&](AVCodecContext* decoder_context,int stream_index,bool video) {
            while(!stop_) {
                int result=avcodec_receive_frame(decoder_context,c.frame);
                if(result==AVERROR(EAGAIN)||result==AVERROR_EOF) break;
                if(result<0) throw Error("Media decode failed");
                double pts=c.frame->best_effort_timestamp==AV_NOPTS_VALUE ? (video?position()-offset_:estimated_audio) : c.frame->best_effort_timestamp*av_q2d(c.format->streams[stream_index]->time_base)-origin;
                if(pts+0.04<discard_before) { av_frame_unref(c.frame); continue; }
                if(video) {
                    int w=c.frame->width,h=c.frame->height;
                    if(w<1 || h<1 || w>1920 || h>1080) throw Error("Unsupported frame dimensions");
                    VideoFrame frame; frame.width=w; frame.height=h; frame.pts=pts;
                    int bytes=av_image_get_buffer_size(AV_PIX_FMT_YUV420P,w,h,1);
                    frame.pixels.resize(bytes);
                    uint8_t* output[4]{}; int strides[4]{};
                    av_image_fill_arrays(output,strides,frame.pixels.data(),AV_PIX_FMT_YUV420P,w,h,1);
                    c.scaler=sws_getCachedContext(c.scaler,w,h,static_cast<AVPixelFormat>(c.frame->format),w,h,AV_PIX_FMT_YUV420P,SWS_BILINEAR,nullptr,nullptr,nullptr);
                    if(!c.scaler) throw Error("Video conversion allocation failed");
                    sws_scale(c.scaler,c.frame->data,c.frame->linesize,0,h,output,strides);
                    std::unique_lock<std::mutex> lock(mutex_);
                    ++metrics_.decoded;
                    cv_.wait(lock,[&]{return stop_||seek_>=0||frames_.size()<6;});
                    if(stop_ || seek_>=0) { av_frame_unref(c.frame); break; }
                    frames_.push_back(std::move(frame));
                } else {
                    int samples=swr_get_out_samples(c.resampler,c.frame->nb_samples);
                    if(samples<0 || samples>48000*2) throw Error("Audio frame exceeds memory limit");
                    std::vector<int16_t> pcm(static_cast<size_t>(samples)*2);
                    uint8_t* output=reinterpret_cast<uint8_t*>(pcm.data());
                    int converted=swr_convert(c.resampler,&output,samples,const_cast<const uint8_t**>(c.frame->extended_data),c.frame->nb_samples);
                    if(converted<0) throw Error("Audio conversion failed");
                    {std::lock_guard<std::mutex> lock(mutex_);metrics_.audio_samples+=converted;}
                    for(int k=0;k<converted*2;++k) pcm[k]=static_cast<int16_t>(pcm[k]*volume_.load());
                    while(!stop_ && seek_<0 && (paused_ || SDL_GetQueuedAudioSize(c.device)>192000/2)) {
                        std::unique_lock<std::mutex> lock(mutex_); cv_.wait_for(lock,std::chrono::milliseconds(10));
                    }
                    if(stop_ || seek_>=0) { av_frame_unref(c.frame); break; }
                    bool seen=audio_seen_.load();
                    if(seen && SDL_GetQueuedAudioSize(c.device)==0 && converted>0) {
                        std::lock_guard<std::mutex> lock(mutex_); ++metrics_.underruns;
                    }
                    if(SDL_QueueAudio(c.device,pcm.data(),converted*4)!=0) throw Error("Audio output queue failed");
                    estimated_audio=pts+converted/48000.0; audio_end_=estimated_audio; audio_seen_=true;
                }
                av_frame_unref(c.frame);
            }
        };
        bool eof=false;
        while(!stop_) {
            if(paused_) { std::unique_lock<std::mutex> lock(mutex_); cv_.wait_for(lock,std::chrono::milliseconds(20)); continue; }
            double target=seek_.exchange(-1);
            if(target>=0) {
                int index=vi>=0?vi:ai;
                int64_t ts=static_cast<int64_t>((target+origin)/av_q2d(c.format->streams[index]->time_base));
                if(avformat_seek_file(c.format,index,INT64_MIN,ts,ts,AVSEEK_FLAG_BACKWARD)<0) throw Error("This stream does not support byte seeking; renegotiate transcoded playback");
                if(c.video) avcodec_flush_buffers(c.video);
                if(c.audio) { avcodec_flush_buffers(c.audio); swr_close(c.resampler); if(swr_init(c.resampler)<0) throw Error("Audio seek reset failed"); }
                if(c.device) SDL_ClearQueuedAudio(c.device);
                { std::lock_guard<std::mutex> lock(mutex_); frames_.clear(); }
                audio_seen_=false; audio_end_=target; estimated_audio=target; anchor_=now()-target; discard_before=target; eof=false;
            }
            if(eof) {
                bool empty; { std::lock_guard<std::mutex> lock(mutex_); empty=frames_.empty(); }
                if(c.device && SDL_GetQueuedAudioSize(c.device)==0 && audio_seen_) { anchor_=now()-audio_end_; audio_seen_=false; }
                if(empty && (!c.device || SDL_GetQueuedAudioSize(c.device)==0)) break;
                std::unique_lock<std::mutex> lock(mutex_); cv_.wait_for(lock,std::chrono::milliseconds(10)); continue;
            }
            int result=av_read_frame(c.format,c.packet);
            if(result==AVERROR_EOF) {
                if(c.video) { avcodec_send_packet(c.video,nullptr); receive(c.video,vi,true); }
                if(c.audio) { avcodec_send_packet(c.audio,nullptr); receive(c.audio,ai,false); }
                eof=true; continue;
            }
            if(result<0) throw Error("Stream interrupted or invalid compressed media");
            { std::lock_guard<std::mutex> lock(mutex_); metrics_.bytes+=c.packet->size; }
            if(c.packet->stream_index==vi && c.video) {
                if(avcodec_send_packet(c.video,c.packet)<0) throw Error("Video packet rejected");
                receive(c.video,vi,true);
            } else if(c.packet->stream_index==ai && c.audio) {
                if(avcodec_send_packet(c.audio,c.packet)<0) throw Error("Audio packet rejected");
                receive(c.audio,ai,false);
            }
            av_packet_unref(c.packet);
        }
        // No UI calls are made on this worker. The SDL device is closed by Context.
        audio_=0;
    } catch(const std::exception& e) {
        audio_=0;
        if(!stop_) { std::lock_guard<std::mutex> lock(mutex_); error_=e.what(); }
    }
    { std::lock_guard<std::mutex> lock(mutex_); metrics_.seconds=now()-began; }
    finished_=true; buffering_=false; cv_.notify_all();
}
}
