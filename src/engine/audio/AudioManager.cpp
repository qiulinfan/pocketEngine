#include "audio/AudioManager.h"
#include "audio/AudioHelper.h"
#include "shared/resources/ResourcePath.h"
#include "scene/Scene.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

bool ShouldLogAudioDiagnostics() {
    return std::getenv("ENGINE_LOG_AUDIO") != nullptr;
}

void LogPlaybackFailure(const char *kind, const std::string &audio_name,
                        int failure_count) {
    if (!ShouldLogAudioDiagnostics()) return;
    if (failure_count > 5 && (failure_count % 25) != 0) return;
    std::cout << "warning: " << kind << " playback failed for ["
              << audio_name << "]: " << Mix_GetError() << std::endl;
}

} // namespace

// initialize SDL_mixer once and reuse it after that
bool AudioManager::Init() {
    if (initialized) return true;
    if (init_attempted) return false;

    // Try audio backend once. In CI/headless machines ALSA/CoreAudio may be
    // missing; repeated retries would spam stderr every frame.
    init_attempted = true;
    const int required_codec_flags = MIX_INIT_OGG | MIX_INIT_MP3;
    const int initialized_codec_flags =
        AudioHelper::Mix_Init(required_codec_flags);
    if ((initialized_codec_flags & required_codec_flags) !=
        required_codec_flags) {
        if (!init_failure_logged) {
            std::cout << "warning: SDL_mixer codec support is partial: "
                      << Mix_GetError() << std::endl;
        }
    }

    const int result =
        AudioHelper::Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 4096);
    if (result != 0) {
        if (!init_failure_logged) {
            std::cout << "warning: audio disabled (failed to open audio device): "
                      << Mix_GetError() << std::endl;
            init_failure_logged = true;
        }
        return false;
    }
    AudioHelper::Mix_AllocateChannels(64);
    AudioHelper::Mix_VolumeMusic(MIX_MAX_VOLUME);
    AudioHelper::Mix_QuerySpec(&actual_output_frequency, &actual_output_format,
                               &actual_output_channels);
    if (ShouldLogAudioDiagnostics()) {
        std::cout << "audio: opened device at " << actual_output_frequency
                  << " Hz, format " << actual_output_format << ", channels "
                  << actual_output_channels << std::endl;
    }
    
    initialized = true;
    return true;
}

void AudioManager::Shutdown() {
    if (initialized) {
        StopAllPlayback();
    }

    for (auto &entry : audio_cache) {
        if (entry.second != nullptr) {
            AudioHelper::Mix_FreeChunk(entry.second);
        }
    }
    audio_cache.clear();

    for (auto &entry : music_cache) {
        if (entry.second != nullptr) {
            AudioHelper::Mix_FreeMusic(entry.second);
        }
    }
    music_cache.clear();

    if (initialized) {
        AudioHelper::Mix_CloseAudio();
    }
    AudioHelper::Mix_Quit();

    initialized = false;
    init_attempted = false;
    init_failure_logged = false;
    playback_enabled = true;
    current_music_name.clear();
    current_music_loops = 0;
    audio_play_failure_count = 0;
    music_play_failure_count = 0;
    actual_output_frequency = 0;
    actual_output_format = 0;
    actual_output_channels = 0;
}

// supports explicit extension and default wav / ogg lookup
std::string AudioManager::FindAudioFile(const std::string &base_name) {
    return ResourcePath::ResolveResourcePath(
        ResourcePath::ResourceSubdirectory("audio"), base_name,
        {".wav", ".ogg", ".mp3"},
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

Mix_Music *AudioManager::LoadMusicTrack(const std::string &audio_name) {
    auto it = music_cache.find(audio_name);
    if (it != music_cache.end()) return it->second;

    const std::string file_path = FindAudioFile(audio_name);
    if (file_path.empty()) {
        std::cout << "error: failed to play music track "
                  << ClipNameSansExtension(audio_name);
        exit(0);
    }

    Mix_Music *music = AudioHelper::Mix_LoadMUS(file_path.c_str());
    if (music == nullptr) {
        std::cout << "error: failed to play music track "
                  << ClipNameSansExtension(audio_name);
        exit(0);
    }

    music_cache[audio_name] = music;
    return music;
}

bool AudioManager::HasAudioClip(const std::string &audio_name) {
    if (audio_cache.find(audio_name) != audio_cache.end()) return true;
    return !FindAudioFile(audio_name).empty();
}

bool AudioManager::HasMusicTrack(const std::string &audio_name) {
    if (music_cache.find(audio_name) != music_cache.end()) return true;
    return !FindAudioFile(audio_name).empty();
}

bool AudioManager::PreloadAudioClip(const std::string &audio_name) {
    if (!Init()) return false;
    if (audio_cache.find(audio_name) != audio_cache.end()) return true;

    const std::string file_path = FindAudioFile(audio_name);
    if (file_path.empty()) return false;

    Mix_Chunk *chunk = AudioHelper::Mix_LoadWAV(file_path.c_str());
    if (chunk == nullptr) return false;

    audio_cache[audio_name] = chunk;
    return true;
}

bool AudioManager::PreloadMusicTrack(const std::string &audio_name) {
    if (!Init()) return false;
    if (music_cache.find(audio_name) != music_cache.end()) return true;

    const std::string file_path = FindAudioFile(audio_name);
    if (file_path.empty()) return false;

    Mix_Music *music = AudioHelper::Mix_LoadMUS(file_path.c_str());
    if (music == nullptr) return false;

    music_cache[audio_name] = music;
    return true;
}

// Play one named clip on the requested channel using the cached chunk.
void AudioManager::PlayAudioClip(const std::string &audio_name, int channel,
                                 int loops) {
    if (!initialized || !playback_enabled) return;
    Mix_Chunk *chunk = LoadAudioClip(audio_name);
    const int started_channel = AudioHelper::Mix_PlayChannel(channel, chunk, loops);
    if (started_channel != -1) return;
    ++audio_play_failure_count;
    LogPlaybackFailure("audio", audio_name, audio_play_failure_count);
}

void AudioManager::PlayMusicTrack(const std::string &audio_name, int loops) {
    if (!initialized || !playback_enabled) return;
    if (IsMusicPlaying() && current_music_name == audio_name &&
        current_music_loops == loops) {
        return;
    }
    Mix_Music *music = LoadMusicTrack(audio_name);
    if (AudioHelper::Mix_PlayMusic(music, loops) == -1) {
        ++music_play_failure_count;
        LogPlaybackFailure("music", audio_name, music_play_failure_count);
        return;
    }
    current_music_name = audio_name;
    current_music_loops = loops;
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
    return AudioHelper::Mix_Playing(channel) != 0;
}

void AudioManager::HaltMusic() {
    if (!initialized) return;
    AudioHelper::Mix_HaltMusic();
    current_music_name.clear();
    current_music_loops = 0;
}

void AudioManager::SetMusicVolume(int volume) {
    if (!initialized) return;
    AudioHelper::Mix_VolumeMusic(volume);
}

bool AudioManager::IsMusicPlaying() {
    if (!initialized) return false;
    return AudioHelper::Mix_PlayingMusic() != 0;
}

void AudioManager::StopAllPlayback() {
    if (!initialized) return;
    AudioHelper::Mix_HaltChannel(-1);
    AudioHelper::Mix_HaltMusic();
    current_music_name.clear();
    current_music_loops = 0;
}

void AudioManager::SetPlaybackEnabled(bool enabled) {
    playback_enabled = enabled;
    if (!playback_enabled) {
        StopAllPlayback();
    }
}

bool AudioManager::IsPlaybackEnabled() {
    return playback_enabled;
}

int AudioManager::GetAudioPlayFailureCount() {
    return audio_play_failure_count;
}

int AudioManager::GetMusicPlayFailureCount() {
    return music_play_failure_count;
}
