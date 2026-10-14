#include "xenonfin/graphics.hpp"
#include "xenonfin/network.hpp"
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace xf {
Graphics::Graphics(bool headless) {
    if(headless) { SDL_setenv("SDL_VIDEODRIVER","dummy",0); SDL_setenv("SDL_AUDIODRIVER","dummy",0); }
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)!=0) throw Error("SDL initialization failed");
    window_=SDL_CreateWindow("XenonFin",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE|(headless?SDL_WINDOW_HIDDEN:0));
    if(!window_) throw Error("Window creation failed");
    renderer_=SDL_CreateRenderer(window_,-1,headless?SDL_RENDERER_SOFTWARE:SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer_) renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer_) throw Error("Renderer creation failed");
    SDL_RenderSetLogicalSize(renderer_,1280,720);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    if(FT_Init_FreeType(&library_)!=0) throw Error("Font initialization failed");
    std::string font;
    if(const char* p=std::getenv("XENONFIN_FONT")) font=p;
    else for(const auto& p:{"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/truetype/freefont/FreeSans.ttf"}) if(std::filesystem::exists(p)) { font=p; break; }
    if(font.empty() || FT_New_Face(library_,font.c_str(),0,&face_)!=0) throw Error("Set XENONFIN_FONT to a TrueType font file");
}
Graphics::~Graphics() {
    for(auto& p:glyphs_) SDL_DestroyTexture(p.second.texture);
    for(auto& p:image_textures_) SDL_DestroyTexture(p.second.texture);
    if(video_) SDL_DestroyTexture(video_);
    if(face_) FT_Done_Face(face_);
    if(library_) FT_Done_FreeType(library_);
    SDL_DestroyRenderer(renderer_); SDL_DestroyWindow(window_); SDL_Quit();
}
void Graphics::begin() { SDL_SetRenderDrawColor(renderer_,12,16,28,255); SDL_RenderClear(renderer_); }
void Graphics::present() { SDL_RenderPresent(renderer_); }
void Graphics::rect(int x,int y,int w,int h,SDL_Color c,bool fill) {
    SDL_SetRenderDrawColor(renderer_,c.r,c.g,c.b,c.a); SDL_Rect r{x,y,w,h};
    if(fill) SDL_RenderFillRect(renderer_,&r); else SDL_RenderDrawRect(renderer_,&r);
}
static unsigned utf8(const std::string& s,size_t& at) {
    unsigned c=static_cast<unsigned char>(s[at++]);
    if(c<128) return c;
    unsigned needed=c<224?1:c<240?2:3, value=c&((1<<(6-needed))-1);
    if(c<192||c>244||at+needed>s.size()) return 0xfffd;
    for(unsigned n=0;n<needed;++n) {
        unsigned next=static_cast<unsigned char>(s[at]);
        if((next&0xc0)!=0x80) return 0xfffd;
        ++at; value=(value<<6)|(next&63);
    }
    return value;
}
void Graphics::text(const std::string& s,int x,int y,int size,SDL_Color color,int max_width) {
    FT_Set_Pixel_Sizes(face_,0,size);
    int initial=x;
    for(size_t at=0;at<s.size();) {
        auto code=utf8(s,at);
        if(code=='\n') break;
        if(FT_Load_Char(face_,code,FT_LOAD_RENDER)!=0) continue;
        auto slot=face_->glyph;
        int advance=slot->advance.x>>6;
        if(x+advance-initial>max_width) break;
        if(slot->bitmap.width && slot->bitmap.rows) {
            auto key=std::make_pair(code,size);
            auto& cached=glyphs_[key]; cached.used=++clock_;
            if(!cached.texture) {
                auto* surface=SDL_CreateRGBSurfaceWithFormat(0,slot->bitmap.width,slot->bitmap.rows,32,SDL_PIXELFORMAT_RGBA32);
                if(!surface) throw Error("Glyph allocation failed");
                for(unsigned row=0;row<slot->bitmap.rows;++row) for(unsigned col=0;col<slot->bitmap.width;++col) {
                    auto* p=static_cast<uint8_t*>(surface->pixels)+row*surface->pitch+col*4;
                    p[0]=p[1]=p[2]=255; p[3]=slot->bitmap.buffer[row*slot->bitmap.pitch+col];
                }
                cached.texture=SDL_CreateTextureFromSurface(renderer_,surface); SDL_FreeSurface(surface);
                SDL_SetTextureBlendMode(cached.texture,SDL_BLENDMODE_BLEND);
            }
            SDL_SetTextureColorMod(cached.texture,color.r,color.g,color.b); SDL_SetTextureAlphaMod(cached.texture,color.a);
            SDL_Rect target{x+slot->bitmap_left,y+size-slot->bitmap_top,static_cast<int>(slot->bitmap.width),static_cast<int>(slot->bitmap.rows)};
            SDL_RenderCopy(renderer_,cached.texture,nullptr,&target);
        }
        x+=advance;
    }
    while(glyphs_.size()>512) {
        auto it=std::min_element(glyphs_.begin(),glyphs_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        SDL_DestroyTexture(it->second.texture); glyphs_.erase(it);
    }
}
void Graphics::wrapped(const std::string& s,int x,int y,int width,int lines,int size) {
    std::istringstream stream(s);
    std::string word,line; int count=0;
    int capacity=std::max(1,width/(size*3/5));
    while(stream>>word) {
        if(line.size()+word.size()+1>static_cast<size_t>(capacity) && !line.empty()) {
            text(line,x,y+count*(size+8),size, {230,234,244,255},width); line.clear();
            if(++count>=lines) return;
        }
        if(!line.empty()) line+=' ';
        line+=word;
    }
    if(!line.empty() && count<lines) text(line,x,y+count*(size+8),size,{230,234,244,255},width);
}
void Graphics::image(const std::shared_ptr<Image>& image,int x,int y,int w,int h) {
    if(!image) return;
    auto& entry=image_textures_[image.get()];entry.used=++clock_;
    if(!entry.texture) {
        entry.image=image;
        entry.texture=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,image->width,image->height);
        if(!entry.texture) return;
        SDL_UpdateTexture(entry.texture,nullptr,image->rgba.data(),image->width*4);
    }
    double scale=std::min(w/static_cast<double>(image->width),h/static_cast<double>(image->height));
    int width=static_cast<int>(image->width*scale),height=static_cast<int>(image->height*scale);
    SDL_Rect r{x+(w-width)/2,y+(h-height)/2,width,height}; SDL_RenderCopy(renderer_,entry.texture,nullptr,&r);
    while(image_textures_.size()>32) {
        auto it=std::min_element(image_textures_.begin(),image_textures_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        SDL_DestroyTexture(it->second.texture);image_textures_.erase(it);
    }
}
void Graphics::video(const VideoFrame& frame) {
    if(frame.pixels.empty()) return;
    if(video_width_!=frame.width || video_height_!=frame.height) {
        SDL_DestroyTexture(video_); video_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,frame.width,frame.height);
        video_width_=frame.width; video_height_=frame.height;
    }
    int ysize=frame.width*frame.height,cwidth=(frame.width+1)/2,cheight=(frame.height+1)/2;
    SDL_UpdateYUVTexture(video_,nullptr,frame.pixels.data(),frame.width,frame.pixels.data()+ysize,cwidth,frame.pixels.data()+ysize+cwidth*cheight,cwidth);
    double scale=std::min(1280.0/frame.width,720.0/frame.height);
    int width=static_cast<int>(frame.width*scale),height=static_cast<int>(frame.height*scale);
    SDL_Rect target{(1280-width)/2,(720-height)/2,width,height}; SDL_RenderCopy(renderer_,video_,nullptr,&target);
}
}
