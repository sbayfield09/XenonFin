#include "xenonfin/keyboard.hpp"
#include <algorithm>
#include <cctype>
namespace xf {
Keyboard::Keyboard(const std::string& title_,const std::string& initial,bool secret) : value(initial),masked(secret),title(title_) {
    layout();
}
void Keyboard::layout() {
    keys.clear();
    for(char c:std::string(symbols?"!@#$%^&*()_+-={}[]|\\:;\"'<>,.?/~`=0123456789":"1234567890qwertyuiopasdfghjkl;zxcvbnm,./@:-_[]!?")) keys.push_back(std::string(1,c));
    keys.insert(keys.end(),{"SPACE","SHIFT","DEL","DONE"});
}
void Keyboard::insert(const std::string& text) {
    if(value.size()+text.size()<=1024 && text.find_first_of("\r\n")==std::string::npos) value+=text;
}
void Keyboard::erase() {
    if(value.empty()) return;
    auto i=value.size()-1;
    while(i>0 && (static_cast<unsigned char>(value[i])&0xc0)==0x80) --i;
    value.resize(i);
}
void Keyboard::input(Action a) {
    if(a==Action::Up) selected=selected>=10?selected-10:selected;
    if(a==Action::Down) selected=std::min(selected+10,keys.size()-1);
    if(a==Action::Left && selected>0) --selected;
    if(a==Action::Right && selected+1<keys.size()) ++selected;
    if(a==Action::Back) cancelled=true;
    if(a==Action::Start) done=true;
    if(a==Action::Delete) erase();
    if(a==Action::Info) { symbols=!symbols; layout(); selected=0; }
    if(a==Action::Select) {
        auto k=keys[selected];
        if(k=="SPACE") insert(" ");
        else if(k=="SHIFT") shift=!shift;
        else if(k=="DEL") erase();
        else if(k=="DONE") done=true;
        else { if(shift) std::transform(k.begin(),k.end(),k.begin(),[](unsigned char c){return std::toupper(c);}); insert(k); }
    }
}
std::string Keyboard::display() const {
    if(!masked) return value;
    size_t count=0;
    for(unsigned char c:value) if((c&0xc0)!=0x80) ++count;
    return std::string(count,'*');
}
}
