#ifndef ENGINE_CORE_HELPER_H
#define ENGINE_CORE_HELPER_H

// Compatibility shim:
// The old monolithic Helper API is now split across engine modules.
// 兼容层: 旧的 Helper API 已拆分到各个 engine 模块中.

#include "core/FrameClock.h"
#include "input/SDLEventHelper.h"
#include "rendering/SDLRenderHelper.h"
#include "particles/RandomEngine.h"
#include "SDL2/SDL.h"
#include "SDL2_image/SDL_image.h"
#include <string>

class Helper {
public:
    static int GetFrameNumber() {
        return FrameClock::GetFrameNumber();
    }

    static SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w,
                                        int h, Uint32 flags) {
        return SDLRenderHelper::SDL_CreateWindow(title, x, y, w, h, flags);
    }

    static SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index,
                                            Uint32 flags) {
        return SDLRenderHelper::SDL_CreateRenderer(window, index, flags);
    }

    static int SDL_PollEvent(SDL_Event *event) {
        return SDLEventHelper::SDL_PollEvent(event);
    }

    static void SDL_RenderPresent(SDL_Renderer *renderer,
                                  bool advance_gameplay_frame = true) {
        SDLRenderHelper::SDL_RenderPresent(renderer, advance_gameplay_frame);
    }

    static void SDL_RenderCopyEx(Actor::UID actor_uid,
                                 const std::string &actor_name,
                                 SDL_Renderer *renderer, SDL_Texture *texture,
                                 const SDL_FRect *srcrect,
                                 const SDL_FRect *dstrect, float angle,
                                 const SDL_FPoint *center,
                                 SDL_RendererFlip flip) {
        SDLRenderHelper::SDL_RenderCopyEx(actor_uid, actor_name, renderer,
                                          texture,
                                          srcrect, dstrect, angle, center, flip);
    }

    static void SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                               const SDL_FRect *srcrect,
                               const SDL_FRect *dstrect) {
        SDLRenderHelper::SDL_RenderCopy(renderer, texture, srcrect, dstrect);
    }

    static void SDL_QueryTexture(SDL_Texture *texture, float *w, float *h) {
        SDLRenderHelper::SDL_QueryTexture(texture, w, h);
    }
};

#endif
