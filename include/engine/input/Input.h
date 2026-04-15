#ifndef INPUT_H
#define INPUT_H

#include "SDL2/SDL.h"
#include "glm/glm.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

class Input {
public:
    // initialize input state caches
    // 初始化输入状态缓存
    static void Init();

    // feed one SDL event into the input state machine
    // 将一个 SDL 事件写入输入状态机
    static void ProcessEvent(const SDL_Event &event);

    // advance transient just-down / just-up states at frame end
    // 在帧末推进 just-down / just-up 这类瞬时状态
    static void LateUpdate();

    static bool GetKey(SDL_Scancode key);
    static bool GetKeyDown(SDL_Scancode key);
    static bool GetKeyUp(SDL_Scancode key);

    // string keycode overloads
    // 字符串 keycode 版本
    static bool GetKey(const std::string &keycode);
    static bool GetKeyDown(const std::string &keycode);
    static bool GetKeyUp(const std::string &keycode);

    static glm::vec2 GetMousePosition();
    static bool GetMouseButton(int button_num);
    static bool GetMouseButtonDown(int button_num);
    static bool GetMouseButtonUp(int button_num);
    static float GetMouseScrollDelta();

    static void HideCursor();
    static void ShowCursor();

private:
    // internal input state machine
    // 内部输入状态机
    enum INPUT_STATE {
        INPUT_STATE_UP,
        INPUT_STATE_JUST_BECAME_DOWN,
        INPUT_STATE_DOWN,
        INPUT_STATE_JUST_BECAME_UP
    };

    struct SDLScancodeHash {
        std::size_t operator()(SDL_Scancode keycode) const noexcept {
            return static_cast<std::size_t>(keycode);
        }
    };

    // parse a string key name into SDL scancode
    // 把字符串按键名解析成 SDL scancode
    static SDL_Scancode ParseKeycode(const std::string &keycode);

    // current implementation only supports left, middle, right
    // 当前只支持左键, 中键, 右键
    static bool IsSupportedMouseButton(int button_num);

    // keyboard state cache and per-frame transition lists
    // 键盘状态缓存和本帧状态变更列表
    static std::unordered_map<SDL_Scancode, INPUT_STATE, SDLScancodeHash> keyboard_states_;
    static std::vector<SDL_Scancode> just_became_down_scancodes_;
    static std::vector<SDL_Scancode> just_became_up_scancodes_;

    // mouse button held / down / up caches
    // 鼠标按住 / 本帧按下 / 本帧松开缓存
    static std::array<bool, 4> mouse_button_held_;
    static std::array<bool, 4> mouse_button_down_;
    static std::array<bool, 4> mouse_button_up_;

    // latest mouse position and wheel delta for this frame
    // 当前帧鼠标位置和滚轮增量
    static glm::vec2 mouse_position_;
    static float mouse_scroll_delta_;
};

#endif
