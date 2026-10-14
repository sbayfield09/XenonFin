#pragma once
#include <string>
#include <vector>
namespace xf {
enum class Action { None,Up,Down,Left,Right,Select,Back,Search,Info,Start,Previous,Next,SeekBack,SeekForward,Delete,Quit };
class Keyboard {
public:
    std::string value;
    std::vector<std::string> keys;
    size_t selected=0;
    bool masked=false,shift=false,symbols=false,done=false,cancelled=false;
    std::string title;
    explicit Keyboard(const std::string& title_="",const std::string& initial="",bool secret=false);
    void input(Action);
    void insert(const std::string& text);
    void erase();
    std::string display() const;
    void layout();
};
}
