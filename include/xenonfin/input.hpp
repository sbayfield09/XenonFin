#pragma once
#include "keyboard.hpp"
#include <SDL.h>
#include <vector>
namespace xf {
class Input {
    SDL_GameController* controller_=nullptr;
    Action held_=Action::None;
    uint32_t next_repeat_=0;
    void connect();
public:
    Input();
    ~Input();
    std::vector<Action> poll(Keyboard* keyboard=nullptr);
};
}
