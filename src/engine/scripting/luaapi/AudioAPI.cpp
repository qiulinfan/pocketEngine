#include "RegistrationDetail.h"
#include "audio/AudioManager.h"
#include "core/Engine.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include <algorithm>
#include <string>

namespace {

using APIRegistrationDetail::g_lua_state;
using APIRegistrationDetail::g_engine;

bool CanRouteAudioCommands() {
    if (g_engine == nullptr) return true;
    return g_engine->IsAudioPlaybackEnabled();
}

bool EnsureAudioInitialized() {
    if (!CanRouteAudioCommands()) return false;
    return AudioManager::Init();
}

void CppAudioPlay(int channel, const std::string &clip_name, bool does_loop) {
    if (!EnsureAudioInitialized()) return;
    const int loops = does_loop ? -1 : 0;
    AudioManager::PlayAudioClip(clip_name, channel, loops);
}

void CppAudioHalt(int channel) {
    if (!EnsureAudioInitialized()) return;
    AudioManager::HaltChannel(channel);
}

void CppAudioSetVolume(int channel, float volume) {
    if (!EnsureAudioInitialized()) return;
    int volume_i = static_cast<int>(volume);
    volume_i = std::clamp(volume_i, 0, 128);
    AudioManager::SetVolume(channel, volume_i);
}

void InjectAudioAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Audio")
        .addFunction("Play", &CppAudioPlay)
        .addFunction("Halt", &CppAudioHalt)
        .addFunction("SetVolume", &CppAudioSetVolume)
        .endNamespace();
}

void CppMusicPlay(const std::string &clip_name, bool does_loop) {
    if (!EnsureAudioInitialized()) return;
    const int loops = does_loop ? -1 : 0;
    AudioManager::PlayMusicTrack(clip_name, loops);
}

void CppMusicHalt() {
    if (!EnsureAudioInitialized()) return;
    AudioManager::HaltMusic();
}

void CppMusicSetVolume(float volume) {
    if (!EnsureAudioInitialized()) return;
    int volume_i = static_cast<int>(volume);
    volume_i = std::clamp(volume_i, 0, 128);
    AudioManager::SetMusicVolume(volume_i);
}

bool CppMusicIsPlaying() {
    if (!EnsureAudioInitialized()) return false;
    return AudioManager::IsMusicPlaying();
}

void InjectMusicAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Music")
        .addFunction("Play", &CppMusicPlay)
        .addFunction("Halt", &CppMusicHalt)
        .addFunction("SetVolume", &CppMusicSetVolume)
        .addFunction("IsPlaying", &CppMusicIsPlaying)
        .endNamespace();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterAudioAPI() {
    InjectAudioAPI();
    InjectMusicAPI();
}

} // namespace APIRegistrationDetail
