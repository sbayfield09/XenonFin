#include "xenonfin/input.hpp"
namespace xf {
Input::Input() { connect(); SDL_StartTextInput(); }
Input::~Input() { if(controller_) SDL_GameControllerClose(controller_); }
void Input::connect() {
    if(controller_ && SDL_GameControllerGetAttached(controller_)) return;
    if(controller_) SDL_GameControllerClose(controller_);
    controller_=nullptr;
    for(int i=0;i<SDL_NumJoysticks();++i) if(SDL_IsGameController(i)) { controller_=SDL_GameControllerOpen(i); if(controller_) break; }
}
static Action button(unsigned b) {
    switch(b) {
        case SDL_CONTROLLER_BUTTON_A:return Action::Select;
        case SDL_CONTROLLER_BUTTON_B:return Action::Back;
        case SDL_CONTROLLER_BUTTON_X:return Action::Search;
        case SDL_CONTROLLER_BUTTON_Y:return Action::Info;
        case SDL_CONTROLLER_BUTTON_START:return Action::Start;
        case SDL_CONTROLLER_BUTTON_BACK:return Action::Delete;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:return Action::Previous;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:return Action::Next;
        default:return Action::None;
    }
}
std::vector<Action> Input::poll(Keyboard* keyboard) {
    std::vector<Action> actions;
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
        if(e.type==SDL_QUIT) actions.push_back(Action::Quit);
        if(e.type==SDL_CONTROLLERDEVICEADDED || e.type==SDL_CONTROLLERDEVICEREMOVED) connect();
        if(e.type==SDL_CONTROLLERBUTTONDOWN) { auto a=button(e.cbutton.button); if(a!=Action::None) actions.push_back(a); }
        if(e.type==SDL_TEXTINPUT && keyboard) keyboard->insert(e.text.text);
        if(e.type==SDL_KEYDOWN) {
            Action a=Action::None;
            switch(e.key.keysym.sym) {
                case SDLK_UP:a=Action::Up;break; case SDLK_DOWN:a=Action::Down;break;
                case SDLK_LEFT:a=Action::Left;break;case SDLK_RIGHT:a=Action::Right;break;
                case SDLK_RETURN:a=keyboard?Action::Start:Action::Select;break;
                case SDLK_SPACE:if(!keyboard) a=Action::Select;break;
                case SDLK_ESCAPE:a=Action::Back;break;case SDLK_BACKSPACE:a=Action::Delete;break;
                case SDLK_TAB:a=Action::Next;break;case SDLK_F1:a=Action::Info;break;
                case SDLK_F2:a=Action::Search;break;case SDLK_F3:a=Action::Start;break;
                case SDLK_PAGEUP:a=Action::SeekBack;break;case SDLK_PAGEDOWN:a=Action::SeekForward;break;
                default:break;
            }
            if(a!=Action::None) actions.push_back(a);
        }
    }
    Action direction=Action::None;
    if(controller_) {
        auto x=SDL_GameControllerGetAxis(controller_,SDL_CONTROLLER_AXIS_LEFTX), y=SDL_GameControllerGetAxis(controller_,SDL_CONTROLLER_AXIS_LEFTY);
        if(SDL_GameControllerGetButton(controller_,SDL_CONTROLLER_BUTTON_DPAD_LEFT)||x<-18000) direction=Action::Left;
        if(SDL_GameControllerGetButton(controller_,SDL_CONTROLLER_BUTTON_DPAD_RIGHT)||x>18000) direction=Action::Right;
        if(SDL_GameControllerGetButton(controller_,SDL_CONTROLLER_BUTTON_DPAD_UP)||y<-18000) direction=Action::Up;
        if(SDL_GameControllerGetButton(controller_,SDL_CONTROLLER_BUTTON_DPAD_DOWN)||y>18000) direction=Action::Down;
        if(SDL_GameControllerGetAxis(controller_,SDL_CONTROLLER_AXIS_TRIGGERLEFT)>18000) direction=Action::SeekBack;
        if(SDL_GameControllerGetAxis(controller_,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>18000) direction=Action::SeekForward;
    }
    auto now=SDL_GetTicks();
    if(direction!=held_) { held_=direction; next_repeat_=now+350; if(direction!=Action::None) actions.push_back(direction); }
    else if(direction!=Action::None && static_cast<int32_t>(now-next_repeat_)>=0) { actions.push_back(direction); next_repeat_=now+160; }
    return actions;
}
}
