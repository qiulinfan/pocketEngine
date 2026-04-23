#ifndef AUDIOHELPER_H
#define AUDIOHELPER_H

#define AUDIO_HELPER_VERSION 0.9

#include "SDL2_mixer/SDL_mixer.h"

class AudioHelper {
public:
    static inline Mix_Chunk *Mix_LoadWAV(const char *file) {
        SDL_RWops *rwops = SDL_RWFromFile(file, "rb");
        if (rwops == nullptr) return nullptr;
        return ::Mix_LoadWAV_RW(rwops, 1);
    }

    static inline Mix_Music *Mix_LoadMUS(const char *file) {
        return ::Mix_LoadMUS(file);
    }

    static inline int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops) {
        return ::Mix_PlayChannelTimed(channel, chunk, loops, -1);
    }

    static inline int Mix_PlayMusic(Mix_Music *music, int loops) {
        return ::Mix_PlayMusic(music, loops);
    }

    static inline int Mix_Init(int flags) {
        return ::Mix_Init(flags);
    }

    static inline int Mix_OpenAudio(int frequency, Uint16 format, int channels,
                                    int chunksize) {
        return ::Mix_OpenAudio(frequency, format, channels, chunksize);
    }

    static inline int Mix_QuerySpec(int *frequency, Uint16 *format,
                                    int *channels) {
        return ::Mix_QuerySpec(frequency, format, channels);
    }

    static inline int Mix_AllocateChannels(int numchans) {
        return ::Mix_AllocateChannels(numchans);
    }

    static inline int Mix_AllocateChannels498() {
        return Mix_AllocateChannels(50);
    }

    static inline void Mix_Pause(int channel) {
        ::Mix_Pause(channel);
    }

    static inline void Mix_Resume(int channel) {
        ::Mix_Resume(channel);
    }

    static inline int Mix_HaltChannel(int channel) {
        return ::Mix_HaltChannel(channel);
    }

    static inline int Mix_Volume(int channel, int volume) {
        return ::Mix_Volume(channel, volume);
    }

    static inline int Mix_HaltMusic(void) {
        return ::Mix_HaltMusic();
    }

    static inline int Mix_VolumeMusic(int volume) {
        return ::Mix_VolumeMusic(volume);
    }

    static inline int Mix_PlayingMusic(void) {
        return ::Mix_PlayingMusic();
    }

    static inline int Mix_Playing(int channel) {
        return ::Mix_Playing(channel);
    }

    static inline void Mix_FreeChunk(Mix_Chunk *chunk) {
        ::Mix_FreeChunk(chunk);
    }

    static inline void Mix_FreeMusic(Mix_Music *music) {
        ::Mix_FreeMusic(music);
    }

    static inline void Mix_CloseAudio(void) {
        ::Mix_CloseAudio();
    }

    static inline void Mix_Quit(void) {
        ::Mix_Quit();
    }
};

#endif
