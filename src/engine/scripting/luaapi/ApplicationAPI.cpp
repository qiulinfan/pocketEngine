#include "RegistrationDetail.h"
#include "core/FrameClock.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

using APIRegistrationDetail::g_lua_state;

void CppLog(const std::string &message) {
    std::cout << message << '\n';
}

void InjectDebugAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Debug")
        .addFunction("Log", &CppLog)
        .endNamespace();
}

void CppApplicationQuit() {
    std::exit(0);
}

void CppApplicationSleep(int milliseconds) {
    if (milliseconds < 0) milliseconds = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int CppApplicationGetFrame() {
    return FrameClock::GetFrameNumber();
}

void CppApplicationOpenURL(const std::string &url) {
    std::string command;
#ifdef _WIN32
    command = "start \"\" \"" + url + "\"";
#elif __APPLE__
    command = "open \"" + url + "\"";
#else
    command = "xdg-open \"" + url + "\"";
#endif
    std::system(command.c_str());
}

void InjectApplicationAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Application")
        .addFunction("Quit", &CppApplicationQuit)
        .addFunction("Sleep", &CppApplicationSleep)
        .addFunction("GetFrame", &CppApplicationGetFrame)
        .addFunction("OpenURL", &CppApplicationOpenURL)
        .endNamespace();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterApplicationAndDebugAPI() {
    InjectDebugAPI();
    InjectApplicationAPI();
}

} // namespace APIRegistrationDetail
