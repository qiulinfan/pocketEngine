#include "input/Input.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

std::unordered_map<SDL_Scancode, Input::INPUT_STATE, Input::SDLScancodeHash> Input::keyboard_states_ = {};
std::vector<SDL_Scancode> Input::just_became_down_scancodes_ = {};
std::vector<SDL_Scancode> Input::just_became_up_scancodes_ = {};
std::array<bool, 4> Input::mouse_button_held_ = {};
std::array<bool, 4> Input::mouse_button_down_ = {};
std::array<bool, 4> Input::mouse_button_up_ = {};
glm::vec2 Input::mouse_position_ = glm::vec2(0.0f, 0.0f);
float Input::mouse_scroll_delta_ = 0.0f;

namespace {

std::string ToLowerASCII(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
} // namespace

// initialize input state caches
void Input::Init() {
    keyboard_states_.clear();
    keyboard_states_.reserve(SDL_NUM_SCANCODES);
    for (int code = SDL_SCANCODE_UNKNOWN; code < SDL_NUM_SCANCODES; ++code) {
        keyboard_states_[static_cast<SDL_Scancode>(code)] = INPUT_STATE_UP;
    }

    just_became_down_scancodes_.clear();
    just_became_up_scancodes_.clear();
    mouse_button_held_.fill(false);
    mouse_button_down_.fill(false);
    mouse_button_up_.fill(false);
    mouse_position_ = glm::vec2(0.0f, 0.0f);
    mouse_scroll_delta_ = 0.0f;
}

// feed one SDL event into the input state machine
void Input::ProcessEvent(const SDL_Event &event) {
    if (event.type == SDL_KEYDOWN) {
        const SDL_Scancode key = event.key.keysym.scancode;
        if (key <= SDL_SCANCODE_UNKNOWN || key >= SDL_NUM_SCANCODES) return;

        INPUT_STATE &state = keyboard_states_[key];
        if (state == INPUT_STATE_DOWN || state == INPUT_STATE_JUST_BECAME_DOWN) return;
        state = INPUT_STATE_JUST_BECAME_DOWN;
        just_became_down_scancodes_.push_back(key);
    } 
    else if (event.type == SDL_KEYUP) {
        const SDL_Scancode key = event.key.keysym.scancode;
        if (key <= SDL_SCANCODE_UNKNOWN || key >= SDL_NUM_SCANCODES) return;

        INPUT_STATE &state = keyboard_states_[key];
        state = INPUT_STATE_JUST_BECAME_UP;
        just_became_up_scancodes_.push_back(key);
    } 
    else if (event.type == SDL_MOUSEMOTION) {
        mouse_position_.x = static_cast<float>(event.motion.x);
        mouse_position_.y = static_cast<float>(event.motion.y);
    } 
    else if (event.type == SDL_MOUSEBUTTONDOWN) {
        const int button = static_cast<int>(event.button.button);
        if (IsSupportedMouseButton(button)) {
            if (!mouse_button_held_[button]) {
                mouse_button_down_[button] = true;
                mouse_button_up_[button] = false;
            }
            mouse_button_held_[button] = true;
        }
    } 
    else if (event.type == SDL_MOUSEBUTTONUP) {
        const int button = static_cast<int>(event.button.button);
        if (IsSupportedMouseButton(button)) {
            if (mouse_button_held_[button]) {
                mouse_button_up_[button] = true;
                mouse_button_down_[button] = false;
            }
            mouse_button_held_[button] = false;
        }
    } 
    else if (event.type == SDL_MOUSEWHEEL) {
        mouse_scroll_delta_ += event.wheel.preciseY;
    }
}

// advance transient just-down / just-up states at frame end
void Input::LateUpdate() {
    for (SDL_Scancode code : just_became_down_scancodes_) {
        if (keyboard_states_[code] == INPUT_STATE_JUST_BECAME_DOWN) {
            keyboard_states_[code] = INPUT_STATE_DOWN;
        }
    }

    for (SDL_Scancode code : just_became_up_scancodes_) {
        if (keyboard_states_[code] == INPUT_STATE_JUST_BECAME_UP) {
            keyboard_states_[code] = INPUT_STATE_UP;
        }
    }

    just_became_down_scancodes_.clear();
    just_became_up_scancodes_.clear();
    mouse_button_down_.fill(false);
    mouse_button_up_.fill(false);
    mouse_scroll_delta_ = 0.0f;
}

bool Input::GetKey(SDL_Scancode key) {
    if (key <= SDL_SCANCODE_UNKNOWN || key >= SDL_NUM_SCANCODES) return false;
    const INPUT_STATE state = keyboard_states_[key];
    return state == INPUT_STATE_DOWN || state == INPUT_STATE_JUST_BECAME_DOWN;
}

bool Input::GetKeyDown(SDL_Scancode key) {
    if (key <= SDL_SCANCODE_UNKNOWN || key >= SDL_NUM_SCANCODES) return false;
    return keyboard_states_[key] == INPUT_STATE_JUST_BECAME_DOWN;
}

bool Input::GetKeyUp(SDL_Scancode key) {
    if (key <= SDL_SCANCODE_UNKNOWN || key >= SDL_NUM_SCANCODES) return false;
    return keyboard_states_[key] == INPUT_STATE_JUST_BECAME_UP;
}

// parse a string key name into SDL scancode
SDL_Scancode Input::ParseKeycode(const std::string &keycode) {
    static const std::unordered_map<std::string, SDL_Scancode> kKeyMap = {
        {"up", SDL_SCANCODE_UP},
        {"down", SDL_SCANCODE_DOWN},
        {"right", SDL_SCANCODE_RIGHT},
        {"left", SDL_SCANCODE_LEFT},
        {"escape", SDL_SCANCODE_ESCAPE},
        {"lshift", SDL_SCANCODE_LSHIFT},
        {"rshift", SDL_SCANCODE_RSHIFT},
        {"lctrl", SDL_SCANCODE_LCTRL},
        {"rctrl", SDL_SCANCODE_RCTRL},
        {"lalt", SDL_SCANCODE_LALT},
        {"ralt", SDL_SCANCODE_RALT},
        {"tab", SDL_SCANCODE_TAB},
        {"return", SDL_SCANCODE_RETURN},
        {"enter", SDL_SCANCODE_RETURN},
        {"backspace", SDL_SCANCODE_BACKSPACE},
        {"delete", SDL_SCANCODE_DELETE},
        {"insert", SDL_SCANCODE_INSERT},
        {"space", SDL_SCANCODE_SPACE},
        {"a", SDL_SCANCODE_A},
        {"b", SDL_SCANCODE_B},
        {"c", SDL_SCANCODE_C},
        {"d", SDL_SCANCODE_D},
        {"e", SDL_SCANCODE_E},
        {"f", SDL_SCANCODE_F},
        {"g", SDL_SCANCODE_G},
        {"h", SDL_SCANCODE_H},
        {"i", SDL_SCANCODE_I},
        {"j", SDL_SCANCODE_J},
        {"k", SDL_SCANCODE_K},
        {"l", SDL_SCANCODE_L},
        {"m", SDL_SCANCODE_M},
        {"n", SDL_SCANCODE_N},
        {"o", SDL_SCANCODE_O},
        {"p", SDL_SCANCODE_P},
        {"q", SDL_SCANCODE_Q},
        {"r", SDL_SCANCODE_R},
        {"s", SDL_SCANCODE_S},
        {"t", SDL_SCANCODE_T},
        {"u", SDL_SCANCODE_U},
        {"v", SDL_SCANCODE_V},
        {"w", SDL_SCANCODE_W},
        {"x", SDL_SCANCODE_X},
        {"y", SDL_SCANCODE_Y},
        {"z", SDL_SCANCODE_Z},
        {"0", SDL_SCANCODE_0},
        {"1", SDL_SCANCODE_1},
        {"2", SDL_SCANCODE_2},
        {"3", SDL_SCANCODE_3},
        {"4", SDL_SCANCODE_4},
        {"5", SDL_SCANCODE_5},
        {"6", SDL_SCANCODE_6},
        {"7", SDL_SCANCODE_7},
        {"8", SDL_SCANCODE_8},
        {"9", SDL_SCANCODE_9},
        {"/", SDL_SCANCODE_SLASH},
        {";", SDL_SCANCODE_SEMICOLON},
        {"=", SDL_SCANCODE_EQUALS},
        {"-", SDL_SCANCODE_MINUS},
        {".", SDL_SCANCODE_PERIOD},
        {",", SDL_SCANCODE_COMMA},
        {"[", SDL_SCANCODE_LEFTBRACKET},
        {"]", SDL_SCANCODE_RIGHTBRACKET},
        {"\\", SDL_SCANCODE_BACKSLASH},
        {"'", SDL_SCANCODE_APOSTROPHE}
    };

    const std::string normalized = ToLowerASCII(keycode);
    auto it = kKeyMap.find(normalized);
    if (it == kKeyMap.end()) return SDL_SCANCODE_UNKNOWN;
    return it->second;
}

// string keycode overloads
bool Input::GetKey(const std::string &keycode) {
    return GetKey(ParseKeycode(keycode));
}

// string keycode overloads
bool Input::GetKeyDown(const std::string &keycode) {
    return GetKeyDown(ParseKeycode(keycode));
}

// string keycode overloads
bool Input::GetKeyUp(const std::string &keycode) {
    return GetKeyUp(ParseKeycode(keycode));
}

// current implementation only supports left, middle, right
bool Input::IsSupportedMouseButton(int button_num) {
    return button_num >= 1 && button_num <= 3;
}

glm::vec2 Input::GetMousePosition() {
    return mouse_position_;
}

bool Input::GetMouseButton(int button_num) {
    if (!IsSupportedMouseButton(button_num)) return false;
    return mouse_button_held_[button_num];
}

bool Input::GetMouseButtonDown(int button_num) {
    if (!IsSupportedMouseButton(button_num)) return false;
    return mouse_button_down_[button_num];
}


bool Input::GetMouseButtonUp(int button_num) {
    if (!IsSupportedMouseButton(button_num)) return false;
    return mouse_button_up_[button_num];
}

float Input::GetMouseScrollDelta() {
    return mouse_scroll_delta_;
}

void Input::HideCursor() {
    SDL_ShowCursor(SDL_DISABLE);
}


void Input::ShowCursor() {
    SDL_ShowCursor(SDL_ENABLE);
}
