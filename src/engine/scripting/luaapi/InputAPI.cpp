#include "RegistrationDetail.h"
#include "input/Input.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include "glm/glm.hpp"
#include <string>

namespace {

using APIRegistrationDetail::g_lua_state;

bool CppInputGetKey(const std::string &keycode) {
    return Input::GetKey(keycode);
}

bool CppInputGetKeyDown(const std::string &keycode) {
    return Input::GetKeyDown(keycode);
}

bool CppInputGetKeyUp(const std::string &keycode) {
    return Input::GetKeyUp(keycode);
}

glm::vec2 CppInputGetMousePosition() {
    return Input::GetMousePosition();
}

bool CppInputGetMouseButton(int button_num) {
    return Input::GetMouseButton(button_num);
}

bool CppInputGetMouseButtonDown(int button_num) {
    return Input::GetMouseButtonDown(button_num);
}

bool CppInputGetMouseButtonUp(int button_num) {
    return Input::GetMouseButtonUp(button_num);
}

float CppInputGetMouseScrollDelta() {
    return Input::GetMouseScrollDelta();
}

void CppInputHideCursor() {
    Input::HideCursor();
}

void CppInputShowCursor() {
    Input::ShowCursor();
}

void InjectInputAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<glm::vec2>("vec2")
        .addProperty("x", &glm::vec2::x)
        .addProperty("y", &glm::vec2::y)
        .endClass();

    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Input")
        .addFunction("GetKey", &CppInputGetKey)
        .addFunction("GetKeyDown", &CppInputGetKeyDown)
        .addFunction("GetKeyUp", &CppInputGetKeyUp)
        .addFunction("GetMousePosition", &CppInputGetMousePosition)
        .addFunction("GetMouseButton", &CppInputGetMouseButton)
        .addFunction("GetMouseButtonDown", &CppInputGetMouseButtonDown)
        .addFunction("GetMouseButtonUp", &CppInputGetMouseButtonUp)
        .addFunction("GetMouseScrollDelta", &CppInputGetMouseScrollDelta)
        .addFunction("HideCursor", &CppInputHideCursor)
        .addFunction("ShowCursor", &CppInputShowCursor)
        .endNamespace();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterInputAPI() {
    InjectInputAPI();
}

} // namespace APIRegistrationDetail
