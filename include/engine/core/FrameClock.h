#ifndef ENGINE_CORE_FRAME_CLOCK_H
#define ENGINE_CORE_FRAME_CLOCK_H

#include "SDL2/SDL.h"
#include <algorithm>

namespace FrameClock {

// Runtime frame index visible to gameplay/debug APIs.
// 对外可见的运行时帧编号.
inline int frame_number = 0;
inline Uint32 current_frame_start_timestamp = 0;
inline bool initialized = false;

inline int GetFrameNumber() {
    return frame_number;
}

inline void Reset() {
    current_frame_start_timestamp = SDL_GetTicks();
    frame_number = 0;
    initialized = true;
}

inline void EnsureInitialized() {
    if (initialized) return;
    Reset();
}

inline void DelayToMaintain60FPS() {
    EnsureInitialized();
    const Uint32 current_frame_end_timestamp = SDL_GetTicks();
    const Uint32 current_frame_duration_milliseconds =
        current_frame_end_timestamp - current_frame_start_timestamp;
    const Uint32 desired_frame_duration_milliseconds = 16;
    const int delay_ticks =
        std::max(static_cast<int>(desired_frame_duration_milliseconds) -
                     static_cast<int>(current_frame_duration_milliseconds),
                 0);
    if (delay_ticks > 0) {
        ::SDL_Delay(static_cast<Uint32>(delay_ticks));
    }
    current_frame_start_timestamp = SDL_GetTicks();
}

inline void AdvanceFrame() {
    EnsureInitialized();
    ++frame_number;
}

inline void DelayAndAdvanceFrame() {
    DelayToMaintain60FPS();
    AdvanceFrame();
}

} // namespace FrameClock

#endif
