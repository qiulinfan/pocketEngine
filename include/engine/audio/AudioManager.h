#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include "SDL2_mixer/SDL_mixer.h"
#include <string>
#include <unordered_map>


class AudioManager {
public:
    // initialize SDL_mixer once and reuse it after that
    // 初始化 SDL_mixer. 重复调用会直接复用
    static bool Init();

    // load audio by name and cache it
    // 按名称加载音频并缓存
    static Mix_Chunk *LoadAudioClip(const std::string &audio_name);

    static void PlayAudioClip(const std::string &audio_name, int channel,
                              int loops);
    static void HaltChannel(int channel);
    static void SetVolume(int channel, int volume);

private:
    // supports explicit extension and default wav / ogg lookup
    // 支持显式扩展名, 也会默认查找 wav / ogg
    static std::string FindAudioFile(const std::string &base_name);

    // remove the extension from a clip name, mainly for error output
    // 去掉音频扩展名, 主要给报错输出使用
    static std::string ClipNameSansExtension(const std::string &audio_name);

    // audio system init flag
    // 音频系统初始化标记
    static inline bool initialized = false;
    // 每次音频 API 调用都重试初始化会刷屏, 这里做一次性尝试
    static inline bool init_attempted = false;
    // 音频设备不可用时只打印一次警告
    static inline bool init_failure_logged = false;

    // loaded audio cache
    // 已加载音频缓存
    static inline std::unordered_map<std::string, Mix_Chunk *> audio_cache;
};

#endif
