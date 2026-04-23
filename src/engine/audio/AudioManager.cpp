#include "audio/AudioManager.h"
#include "audio/AudioHelper.h"
#include "shared/resources/ResourcePath.h"
#include "scene/Scene.h"
#include <cstdlib>
#include <iostream>

// initialize SDL_mixer once and reuse it after that
bool AudioManager::Init() {
    if (initialized) return true;
    if (init_attempted) return false;

    // Try audio backend once. In CI/headless machines ALSA/CoreAudio may be
    // missing; repeated retries would spam stderr every frame.
    init_attempted = true;
    const int result =
        AudioHelper::Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    if (result != 0) {
        if (!init_failure_logged) {
            std::cout << "warning: audio disabled (failed to open audio device): "
                      << Mix_GetError() << std::endl;
            init_failure_logged = true;
        }
        return false;
    }
    AudioHelper::Mix_AllocateChannels(50);

    initialized = true;
    return true;
}

// supports explicit extension and default wav / ogg lookup
std::string AudioManager::FindAudioFile(const std::string &base_name) {
    return ResourcePath::ResolveResourcePath(
        "resources/audio", base_name, {".wav", ".ogg"},
        Scene::GetActiveSceneSubdirectory());
}

// remove the extension from a clip name, mainly for error output
std::string AudioManager::ClipNameSansExtension(const std::string &audio_name) {
    size_t dot_pos = audio_name.find_last_of('.');
    if (dot_pos == std::string::npos) return audio_name;
    return audio_name.substr(0, dot_pos);
}

// load audio by name and cache it
Mix_Chunk *AudioManager::LoadAudioClip(const std::string &audio_name) {
    auto it = audio_cache.find(audio_name);
    if (it != audio_cache.end()) return it->second;

    std::string file_path = FindAudioFile(audio_name);
    if (file_path.empty()) {
        std::cout << "error: failed to play audio clip "
                  << ClipNameSansExtension(audio_name);
        exit(0);
    }

    Mix_Chunk *chunk = AudioHelper::Mix_LoadWAV(file_path.c_str());
    if (chunk == nullptr) {
        std::cout << "error: failed to play audio clip "
                  << ClipNameSansExtension(audio_name);
        exit(0);
    }

    audio_cache[audio_name] = chunk;
    return chunk;
}

bool AudioManager::HasAudioClip(const std::string &audio_name) {
    if (audio_name.empty()) return false;
    if (audio_cache.find(audio_name) != audio_cache.end()) return true;
    return !FindAudioFile(audio_name).empty();
}

bool AudioManager::PreloadAudioClip(const std::string &audio_name) {
    if (!Init()) return false;
    if (!HasAudioClip(audio_name)) return false;
    return LoadAudioClip(audio_name) != nullptr;
}

// Play one named clip on the requested channel using the cached chunk.
void AudioManager::PlayAudioClip(const std::string &audio_name, int channel,
                                 int loops) {
    if (!initialized) return;
    Mix_Chunk *chunk = LoadAudioClip(audio_name);
    AudioHelper::Mix_PlayChannel(channel, chunk, loops);
}

// Stop playback on one mixer channel, or all channels when channel == -1.
void AudioManager::HaltChannel(int channel) {
    if (!initialized) return;
    AudioHelper::Mix_HaltChannel(channel);
}

// Update mixer volume for one channel without touching the cached clip data.
void AudioManager::SetVolume(int channel, int volume) {
    if (!initialized) return;
    AudioHelper::Mix_Volume(channel, volume);
}

bool AudioManager::IsChannelPlaying(int channel) {
    if (!initialized) return false;
    return ::Mix_Playing(channel) != 0;
}

bool AudioManager::IsPlaybackEnabled() {
    return initialized || Init();
}
