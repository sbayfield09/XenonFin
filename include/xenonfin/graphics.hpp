#pragma once
#include "images.hpp"
#include "playback.hpp"
#include <SDL.h>
#include <map>
#include <memory>
#include <string>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace xf {
class Graphics {
    SDL_Window* window_=nullptr;
    SDL_Renderer* renderer_=nullptr;
    FT_Library library_=nullptr;
    FT_Face face_=nullptr;
    SDL_Texture* video_=nullptr;
    int video_width_=0,video_height_=0;
    struct Cached { SDL_Texture* texture=nullptr; uint64_t used=0; };
    std::map<std::pair<unsigned,int>,Cached> glyphs_;
    struct ImageTexture { std::shared_ptr<Image> image; SDL_Texture* texture=nullptr; uint64_t used=0; };
    std::map<const Image*,ImageTexture> image_textures_;
    uint64_t clock_=0;
public:
    explicit Graphics(bool headless=false);
    ~Graphics();
    void begin();
    void present();
    void rect(int x,int y,int w,int h,SDL_Color color,bool fill=true);
    void text(const std::string&,int x,int y,int size=24,SDL_Color color={230,234,244,255},int max_width=1180);
    void wrapped(const std::string&,int x,int y,int width,int lines=5,int size=22);
    void image(const std::shared_ptr<Image>&,int x,int y,int w,int h);
    void video(const VideoFrame&);
    SDL_Renderer* renderer() { return renderer_; }
};
}
