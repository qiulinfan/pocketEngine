#include "input/Input.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

std::unordered_map<SDL_Scancode, Input::INPUT_STATE, Input::SDLScancodeHash> Input::keyboard_states_ = {};
std::vector<SDL_Scancode> Input::just_became_down_scancodes_ = {};
std::vector<SDL_Scancode> Input::just_became_up_scancodes_ = {};
std::unordered_map<SDL_Keycode, Input::INPUT_STATE, Input::SDLKeycodeHash> Input::keycode_states_ = {};
std::vector<SDL_Keycode> Input::just_became_down_keycodes_ = {};
std::vector<SDL_Keycode> Input::just_became_up_keycodes_ = {};
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

bool IsValidScancode(SDL_Scancode key) {
    return key > SDL_SCANCODE_UNKNOWN && key < SDL_NUM_SCANCODES;
}

bool IsValidKeycode(SDL_Keycode key) {
    return key != SDLK_UNKNOWN;
}

SDL_Scancode ResolveKeyboardEventScancode(const SDL_KeyboardEvent &event) {
    if (IsValidScancode(event.keysym.scancode)) {
        return event.keysym.scancode;
    }

    const SDL_Scancode fallback = SDL_GetScancodeFromKey(event.keysym.sym);
    return IsValidScancode(fallback) ? fallback : SDL_SCANCODE_UNKNOWN;
}

bool IsHardwareKeyDown(SDL_Scancode key) {
    if (!IsValidScancode(key)) return false;

    int key_count = 0;
    const Uint8 *keyboard_state = SDL_GetKeyboardState(&key_count);
    if (keyboard_state == nullptr || static_cast<int>(key) >= key_count) {
        return false;
    }
    return keyboard_state[key] != 0;
}

bool ShouldLogInputDiagnostics() {
    return std::getenv("ENGINE_LOG_INPUT") != nullptr;
}

void LogKeyboardEvent(const SDL_KeyboardEvent &event, SDL_Scancode resolved_key) {
    if (!ShouldLogInputDiagnostics()) return;

    static int logged_events = 0;
    if (logged_events >= 80) return;
    ++logged_events;

    const char *type = event.type == SDL_KEYDOWN ? "down" : "up";
    std::cout << "input: key " << type
              << " scancode=" << static_cast<int>(event.keysym.scancode)
              << " resolved=" << static_cast<int>(resolved_key)
              << " scancode_name=" << SDL_GetScancodeName(resolved_key)
              << " keycode=" << event.keysym.sym
              << " key_name=" << SDL_GetKeyName(event.keysym.sym)
              << " repeat=" << static_cast<int>(event.repeat) << std::endl;
}
} // namespace

// initialize input state caches
void Input::Init() {
    keyboard_states_.clear();
    keyboard_states_.reserve(SDL_NUM_SCANCODES);
    for (int code = SDL_SCANCODE_UNKNOWN; code < SDL_NUM_SCANCODES; ++code) {
        keyboard_states_[static_cast<SDL_Scancode>(code)] = INPUT_STATE_UP;
    }
    keycode_states_.clear();

    just_became_down_scancodes_.clear();
    just_became_up_scancodes_.clear();
    just_became_down_keycodes_.clear();
    just_became_up_keycodes_.clear();
    mouse_button_held_.fill(false);
    mouse_button_down_.fill(false);
    mouse_button_up_.fill(false);
    mouse_position_ = glm::vec2(0.0f, 0.0f);
    mouse_scroll_delta_ = 0.0f;
}

// feed one SDL event into the input state machine
void Input::ProcessEvent(const SDL_Event &event) {
    if (event.type == SDL_KEYDOWN) {
        const SDL_Scancode key = ResolveKeyboardEventScancode(event.key);
        LogKeyboardEvent(event.key, key);
        const SDL_Keycode keycode = event.key.keysym.sym;

        if (IsValidScancode(key)) {
            INPUT_STATE &state = keyboard_states_[key];
            if (state != INPUT_STATE_DOWN && state != INPUT_STATE_JUST_BECAME_DOWN) {
                state = INPUT_STATE_JUST_BECAME_DOWN;
                just_became_down_scancodes_.push_back(key);
            }
        }
        if (IsValidKeycode(keycode)) {
            INPUT_STATE &state = keycode_states_[keycode];
            if (state != INPUT_STATE_DOWN && state != INPUT_STATE_JUST_BECAME_DOWN) {
                state = INPUT_STATE_JUST_BECAME_DOWN;
                just_became_down_keycodes_.push_back(keycode);
            }
        }
    } 
    else if (event.type == SDL_KEYUP) {
        const SDL_Scancode key = ResolveKeyboardEventScancode(event.key);
        LogKeyboardEvent(event.key, key);
        const SDL_Keycode keycode = event.key.keysym.sym;

        if (IsValidScancode(key)) {
            INPUT_STATE &state = keyboard_states_[key];
            state = INPUT_STATE_JUST_BECAME_UP;
            just_became_up_scancodes_.push_back(key);
        }
        if (IsValidKeycode(keycode)) {
            INPUT_STATE &state = keycode_states_[keycode];
            state = INPUT_STATE_JUST_BECAME_UP;
            just_became_up_keycodes_.push_back(keycode);
        }
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
    for (SDL_Keycode code : just_became_down_keycodes_) {
        if (keycode_states_[code] == INPUT_STATE_JUST_BECAME_DOWN) {
            keycode_states_[code] = INPUT_STATE_DOWN;
        }
    }

    for (SDL_Keycode code : just_became_up_keycodes_) {
        if (keycode_states_[code] == INPUT_STATE_JUST_BECAME_UP) {
            keycode_states_[code] = INPUT_STATE_UP;
        }
    }

    just_became_down_scancodes_.clear();
    just_became_up_scancodes_.clear();
    just_became_down_keycodes_.clear();
    just_became_up_keycodes_.clear();
    mouse_button_down_.fill(false);
    mouse_button_up_.fill(false);
    mouse_scroll_delta_ = 0.0f;
}

bool Input::GetKey(SDL_Scancode key) {
    if (!IsValidScancode(key)) return false;
    if (IsHardwareKeyDown(key)) return true;
    const INPUT_STATE state = keyboard_states_[key];
    return state == INPUT_STATE_DOWN || state == INPUT_STATE_JUST_BECAME_DOWN;
}

bool Input::GetKeyDown(SDL_Scancode key) {
    if (!IsValidScancode(key)) return false;
    return keyboard_states_[key] == INPUT_STATE_JUST_BECAME_DOWN;
}

bool Input::GetKeyUp(SDL_Scancode key) {
    if (!IsValidScancode(key)) return false;
    return keyboard_states_[key] == INPUT_STATE_JUST_BECAME_UP;
}

// parse a string key name into SDL scancode
SDL_Scancode Input::ParseKeycode(const std::string &keycode) {
    return ParseKeyBinding(keycode).scancode;
}

Input::KeyBinding Input::ParseKeyBinding(const std::string &keycode) {
    static const std::unordered_map<std::string, KeyBinding> kKeyMap = {
        {"up", {SDL_SCANCODE_UP, SDLK_UP}},
        {"down", {SDL_SCANCODE_DOWN, SDLK_DOWN}},
        {"right", {SDL_SCANCODE_RIGHT, SDLK_RIGHT}},
        {"left", {SDL_SCANCODE_LEFT, SDLK_LEFT}},
        {"escape", {SDL_SCANCODE_ESCAPE, SDLK_ESCAPE}},
        {"lshift", {SDL_SCANCODE_LSHIFT, SDLK_LSHIFT}},
        {"left shift", {SDL_SCANCODE_LSHIFT, SDLK_LSHIFT}},
        {"rshift", {SDL_SCANCODE_RSHIFT, SDLK_RSHIFT}},
        {"right shift", {SDL_SCANCODE_RSHIFT, SDLK_RSHIFT}},
        {"shift", {SDL_SCANCODE_LSHIFT, SDLK_LSHIFT}},
        {"lctrl", {SDL_SCANCODE_LCTRL, SDLK_LCTRL}},
        {"rctrl", {SDL_SCANCODE_RCTRL, SDLK_RCTRL}},
        {"lalt", {SDL_SCANCODE_LALT, SDLK_LALT}},
        {"ralt", {SDL_SCANCODE_RALT, SDLK_RALT}},
        {"tab", {SDL_SCANCODE_TAB, SDLK_TAB}},
        {"return", {SDL_SCANCODE_RETURN, SDLK_RETURN}},
        {"enter", {SDL_SCANCODE_RETURN, SDLK_RETURN}},
        {"backspace", {SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE}},
        {"delete", {SDL_SCANCODE_DELETE, SDLK_DELETE}},
        {"insert", {SDL_SCANCODE_INSERT, SDLK_INSERT}},
        {"space", {SDL_SCANCODE_SPACE, SDLK_SPACE}},
        {"a", {SDL_SCANCODE_A, SDLK_a}},
        {"b", {SDL_SCANCODE_B, SDLK_b}},
        {"c", {SDL_SCANCODE_C, SDLK_c}},
        {"d", {SDL_SCANCODE_D, SDLK_d}},
        {"e", {SDL_SCANCODE_E, SDLK_e}},
        {"f", {SDL_SCANCODE_F, SDLK_f}},
        {"g", {SDL_SCANCODE_G, SDLK_g}},
        {"h", {SDL_SCANCODE_H, SDLK_h}},
        {"i", {SDL_SCANCODE_I, SDLK_i}},
        {"j", {SDL_SCANCODE_J, SDLK_j}},
        {"k", {SDL_SCANCODE_K, SDLK_k}},
        {"l", {SDL_SCANCODE_L, SDLK_l}},
        {"m", {SDL_SCANCODE_M, SDLK_m}},
        {"n", {SDL_SCANCODE_N, SDLK_n}},
        {"o", {SDL_SCANCODE_O, SDLK_o}},
        {"p", {SDL_SCANCODE_P, SDLK_p}},
        {"q", {SDL_SCANCODE_Q, SDLK_q}},
        {"r", {SDL_SCANCODE_R, SDLK_r}},
        {"s", {SDL_SCANCODE_S, SDLK_s}},
        {"t", {SDL_SCANCODE_T, SDLK_t}},
        {"u", {SDL_SCANCODE_U, SDLK_u}},
        {"v", {SDL_SCANCODE_V, SDLK_v}},
        {"w", {SDL_SCANCODE_W, SDLK_w}},
        {"x", {SDL_SCANCODE_X, SDLK_x}},
        {"y", {SDL_SCANCODE_Y, SDLK_y}},
        {"z", {SDL_SCANCODE_Z, SDLK_z}},
        {"0", {SDL_SCANCODE_0, SDLK_0}},
        {"1", {SDL_SCANCODE_1, SDLK_1}},
        {"2", {SDL_SCANCODE_2, SDLK_2}},
        {"3", {SDL_SCANCODE_3, SDLK_3}},
        {"4", {SDL_SCANCODE_4, SDLK_4}},
        {"5", {SDL_SCANCODE_5, SDLK_5}},
        {"6", {SDL_SCANCODE_6, SDLK_6}},
        {"7", {SDL_SCANCODE_7, SDLK_7}},
        {"8", {SDL_SCANCODE_8, SDLK_8}},
        {"9", {SDL_SCANCODE_9, SDLK_9}},
        {"/", {SDL_SCANCODE_SLASH, SDLK_SLASH}},
        {";", {SDL_SCANCODE_SEMICOLON, SDLK_SEMICOLON}},
        {"=", {SDL_SCANCODE_EQUALS, SDLK_EQUALS}},
        {"-", {SDL_SCANCODE_MINUS, SDLK_MINUS}},
        {".", {SDL_SCANCODE_PERIOD, SDLK_PERIOD}},
        {",", {SDL_SCANCODE_COMMA, SDLK_COMMA}},
        {"[", {SDL_SCANCODE_LEFTBRACKET, SDLK_LEFTBRACKET}},
        {"]", {SDL_SCANCODE_RIGHTBRACKET, SDLK_RIGHTBRACKET}},
        {"\\", {SDL_SCANCODE_BACKSLASH, SDLK_BACKSLASH}},
        {"'", {SDL_SCANCODE_APOSTROPHE, SDLK_QUOTE}}
    };

    const std::string normalized = ToLowerASCII(keycode);
    auto it = kKeyMap.find(normalized);
    if (it == kKeyMap.end()) return {};
    return it->second;
}

// string keycode overloads
bool Input::GetKey(const std::string &keycode) {
    const KeyBinding binding = ParseKeyBinding(keycode);
    if (GetKey(binding.scancode)) return true;
    if (!IsValidKeycode(binding.keycode)) return false;
    const auto found = keycode_states_.find(binding.keycode);
    if (found == keycode_states_.end()) return false;
    return found->second == INPUT_STATE_DOWN ||
           found->second == INPUT_STATE_JUST_BECAME_DOWN;
}

// string keycode overloads
bool Input::GetKeyDown(const std::string &keycode) {
    const KeyBinding binding = ParseKeyBinding(keycode);
    if (GetKeyDown(binding.scancode)) return true;
    if (!IsValidKeycode(binding.keycode)) return false;
    const auto found = keycode_states_.find(binding.keycode);
    return found != keycode_states_.end() &&
           found->second == INPUT_STATE_JUST_BECAME_DOWN;
}

// string keycode overloads
bool Input::GetKeyUp(const std::string &keycode) {
    const KeyBinding binding = ParseKeyBinding(keycode);
    if (GetKeyUp(binding.scancode)) return true;
    if (!IsValidKeycode(binding.keycode)) return false;
    const auto found = keycode_states_.find(binding.keycode);
    return found != keycode_states_.end() &&
           found->second == INPUT_STATE_JUST_BECAME_UP;
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
