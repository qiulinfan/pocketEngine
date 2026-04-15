#include "RegistrationDetail.h"
#include "core/Engine.h"
#include "scene/Actor.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include <string>

namespace {

using APIRegistrationDetail::g_engine;
using APIRegistrationDetail::g_lua_state;

void CppSceneLoad(const std::string &scene_name) {
    if (g_engine == nullptr) return;
    g_engine->RequestSceneLoad(scene_name);
}

std::string CppSceneGetCurrent() {
    if (g_engine == nullptr) return "";
    return g_engine->GetCurrentSceneName();
}

void CppSceneDontDestroy(Actor *actor) {
    if (g_engine == nullptr) return;
    g_engine->MarkActorDontDestroy(actor);
}

void InjectSceneAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Scene")
        .addFunction("Load", &CppSceneLoad)
        .addFunction("GetCurrent", &CppSceneGetCurrent)
        .addFunction("DontDestroy", &CppSceneDontDestroy)
        .endNamespace();
}

void CppCameraSetPosition(float x, float y) {
    if (g_engine == nullptr) return;
    g_engine->SetCameraPosition(x, y);
}

float CppCameraGetPositionX() {
    if (g_engine == nullptr) return 0.0f;
    return g_engine->GetCameraPositionX();
}

float CppCameraGetPositionY() {
    if (g_engine == nullptr) return 0.0f;
    return g_engine->GetCameraPositionY();
}

void CppCameraSetZoom(float zoom_factor) {
    if (g_engine == nullptr) return;
    g_engine->SetCameraZoom(zoom_factor);
}

float CppCameraGetZoom() {
    if (g_engine == nullptr) return 1.0f;
    return g_engine->GetCameraZoom();
}

void InjectCameraAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Camera")
        .addFunction("SetPosition", &CppCameraSetPosition)
        .addFunction("GetPositionX", &CppCameraGetPositionX)
        .addFunction("GetPositionY", &CppCameraGetPositionY)
        .addFunction("SetZoom", &CppCameraSetZoom)
        .addFunction("GetZoom", &CppCameraGetZoom)
        .endNamespace();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterSceneAndCameraAPI() {
    InjectSceneAPI();
    InjectCameraAPI();
}

} // namespace APIRegistrationDetail
