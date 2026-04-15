#ifndef AUDIOHELPER_H
#define AUDIOHELPER_H

#define AUDIO_HELPER_VERSION 0.9

#include "core/FrameClock.h"
#include "SDL2_mixer/SDL_mixer.h"
#include <iostream>

class AudioHelper {
public:
    static inline Mix_Chunk *Mix_LoadWAV(const char *file) {
        SDL_RWops *rwops = SDL_RWFromFile(file, "rb");
        if (rwops == nullptr) return nullptr;
        return ::Mix_LoadWAV_RW(rwops, 1);
    }

    static inline int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops) {
        std::cout << "(Mix_PlayChannel(" << channel << ",?," << loops
                  << ") called on frame " << FrameClock::GetFrameNumber() << ")"
                  << std::endl;
        return ::Mix_PlayChannelTimed(channel, chunk, loops, -1);
    }

    static inline int Mix_OpenAudio(int frequency, Uint16 format, int channels,
                                    int chunksize) {
        return ::Mix_OpenAudio(frequency, format, channels, chunksize);
    }

    static inline int Mix_AllocateChannels(int numchans) {
        return ::Mix_AllocateChannels(numchans);
    }

    static inline int Mix_AllocateChannels498() {
        return Mix_AllocateChannels(50);
    }

    static inline void Mix_Pause(int channel) {
        std::cout << "(Mix_Pause(" << channel << ") called on frame "
                  << FrameClock::GetFrameNumber() << ")" << std::endl;
        ::Mix_Pause(channel);
    }

    static inline void Mix_Resume(int channel) {
        std::cout << "(Mix_Resume(" << channel << ") called on frame "
                  << FrameClock::GetFrameNumber() << ")" << std::endl;
        ::Mix_Resume(channel);
    }

    static inline int Mix_HaltChannel(int channel) {
        std::cout << "(Mix_HaltChannel(" << channel << ") called on frame "
                  << FrameClock::GetFrameNumber() << ")" << std::endl;
        return ::Mix_HaltChannel(channel);
    }

    static inline int Mix_Volume(int channel, int volume) {
        std::cout << "(Mix_Volume(" << channel << "," << volume
                  << ") called on frame " << FrameClock::GetFrameNumber() << ")"
                  << std::endl;
        return ::Mix_Volume(channel, volume);
    }

    static inline void Mix_CloseAudio(void) {
        ::Mix_CloseAudio();
    }
};

#endif
