#ifndef ENGINE_RENDERING_SDL_RENDER_HELPER_H
#define ENGINE_RENDERING_SDL_RENDER_HELPER_H

#include "core/FrameClock.h"
#include "input/SDLEventHelper.h"
#include "SDL2/SDL.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

class SDLRenderHelper {
public:
    enum class RenderLoggerStatus {
        NotInitialized,
        NotEnabled,
        Enabled
    };

    inline static std::string frame_directory_relative_path = "frames";

    static SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w,
                                        int h, Uint32 flags) {
        return ::SDL_CreateWindow(title, x, y, w, h, flags);
    }

    static SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index,
                                            Uint32 flags) {
        SDL_Renderer *renderer = ::SDL_CreateRenderer(window, index, flags);
        if (renderer == nullptr) {
            std::cerr << "Failed to create renderer : " << SDL_GetError()
                      << std::endl;
        }
        return renderer;
    }

    // Present wrapper that maintains frame pacing, and optionally advances the
    // gameplay frame counter for simulation frames.
    static void SDL_RenderPresent(SDL_Renderer *renderer,
                                  bool advance_gameplay_frame = true) {
        if (renderer == nullptr) {
            std::cout << "ERROR : renderer passed to SDL_RenderPresent is null."
                      << std::endl;
            std::exit(0);
        }

        if (!SDLEventHelper::HasInitializedPolling()) {
            std::cout << "ERROR : call SDL_PollEvent before SDL_RenderPresent."
                      << std::endl;
            std::exit(0);
        }

        CaptureFrameIfRecordingEnabled(renderer);
        ::SDL_RenderPresent(renderer);
        // VSync-backed renderers already block to the monitor refresh cadence.
        // Applying an extra SDL_Delay on top of that can create uneven frame
        // pacing that looks like visible hitching despite a healthy average FPS.
        if (!RendererUsesPresentVSync(renderer)) {
            FrameClock::DelayToMaintain60FPS();
        } else {
            FrameClock::EnsureInitialized();
            FrameClock::current_frame_start_timestamp = SDL_GetTicks();
        }
        if (advance_gameplay_frame) {
            FrameClock::AdvanceFrame();
        }
    }

    static void SDL_RenderCopyEx(int actor_id, const std::string &actor_name,
                                 SDL_Renderer *renderer, SDL_Texture *texture,
                                 const SDL_FRect *srcrect,
                                 const SDL_FRect *dstrect, float angle,
                                 const SDL_FPoint *center,
                                 SDL_RendererFlip flip) {
        SDL_Rect *srcrect_i_ptr = nullptr;
        SDL_Rect *dstrect_i_ptr = nullptr;
        SDL_Point *center_i_ptr = nullptr;

        SDL_Rect srcrect_i;
        SDL_Rect dstrect_i;
        SDL_Point center_i;

        if (srcrect != nullptr) {
            srcrect_i = {static_cast<int>(srcrect->x),
                         static_cast<int>(srcrect->y),
                         static_cast<int>(srcrect->w),
                         static_cast<int>(srcrect->h)};
            srcrect_i_ptr = &srcrect_i;
        }

        if (dstrect != nullptr) {
            dstrect_i = {static_cast<int>(dstrect->x),
                         static_cast<int>(dstrect->y),
                         static_cast<int>(dstrect->w),
                         static_cast<int>(dstrect->h)};
            dstrect_i_ptr = &dstrect_i;
        }

        if (center != nullptr) {
            center_i = {static_cast<int>(center->x), static_cast<int>(center->y)};
            center_i_ptr = &center_i;
        }

        ::SDL_RenderCopyEx(renderer, texture, srcrect_i_ptr, dstrect_i_ptr, angle,
                           center_i_ptr, flip);

        CheckForRenderLoggerInit();
        if (render_logger_mode_ != RenderLoggerStatus::Enabled) return;

        float x_scale = 1.0f;
        float y_scale = 1.0f;
        SDL_RenderGetScale(renderer, &x_scale, &y_scale);

        render_logging_file_ << FrameClock::GetFrameNumber() << ":" << actor_id
                             << ":" << actor_name;
        if (dstrect != nullptr) {
            render_logging_file_ << " dstrect " << dstrect->x << " " << dstrect->y
                                 << " " << dstrect->w << " " << dstrect->h;
        }
        render_logging_file_ << " angle " << angle;
        if (center != nullptr) {
            render_logging_file_ << " center " << center->x << " " << center->y;
        }
        render_logging_file_ << " flip " << flip << " renderscale " << x_scale
                             << " " << y_scale << std::endl;
    }

    static void SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                               const SDL_FRect *srcrect,
                               const SDL_FRect *dstrect) {
        SDL_Rect *srcrect_i_ptr = nullptr;
        SDL_Rect *dstrect_i_ptr = nullptr;
        SDL_Rect srcrect_i;
        SDL_Rect dstrect_i;

        if (srcrect != nullptr) {
            srcrect_i = {static_cast<int>(srcrect->x),
                         static_cast<int>(srcrect->y),
                         static_cast<int>(srcrect->w),
                         static_cast<int>(srcrect->h)};
            srcrect_i_ptr = &srcrect_i;
        }

        if (dstrect != nullptr) {
            dstrect_i = {static_cast<int>(dstrect->x),
                         static_cast<int>(dstrect->y),
                         static_cast<int>(dstrect->w),
                         static_cast<int>(dstrect->h)};
            dstrect_i_ptr = &dstrect_i;
        }

        ::SDL_RenderCopy(renderer, texture, srcrect_i_ptr, dstrect_i_ptr);
    }

    static void SDL_QueryTexture(SDL_Texture *texture, float *w, float *h) {
        if (texture == nullptr) return;

        int w_i = 0;
        int h_i = 0;
        ::SDL_QueryTexture(texture, nullptr, nullptr, &w_i, &h_i);

        if (w != nullptr) *w = static_cast<float>(w_i);
        if (h != nullptr) *h = static_cast<float>(h_i);
    }

private:
    inline static RenderLoggerStatus render_logger_mode_ =
        RenderLoggerStatus::NotInitialized;
    inline static std::ofstream render_logging_file_;
    inline static bool frame_capture_initialized_ = false;
    inline static SDL_Surface *frame_capture_surface_ = nullptr;

    static bool RendererUsesPresentVSync(SDL_Renderer *renderer) {
        if (renderer == nullptr) return false;

        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer, &info) != 0) {
            return false;
        }
        return (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    }

    static bool IsEnvVariableSet(const char *name) {
        const char *value = std::getenv(name);
        return value != nullptr;
    }

    static void CheckForRenderLoggerInit() {
        if (render_logger_mode_ != RenderLoggerStatus::NotInitialized) return;

        if (!IsEnvVariableSet("RENDERLOGGER")) {
            render_logger_mode_ = RenderLoggerStatus::NotEnabled;
            return;
        }

        render_logger_mode_ = RenderLoggerStatus::Enabled;
        std::ofstream truncate_file("render_logger.txt", std::ios::out);
        render_logging_file_.open("render_logger.txt", std::ios::app);
        if (!render_logging_file_.is_open()) {
            std::cerr << "Error : failed to open render_logger.txt."
                      << std::endl;
            return;
        }

        render_logging_file_ << "== RENDER LOGGER ==" << std::endl;
        render_logging_file_
            << "Study SDL_RenderCopyEx() calls for render debugging."
            << std::endl;
        render_logging_file_ << "Enable with RENDERLOGGER env var." << std::endl;
        render_logging_file_ << "frame:actor_id:actor_name" << std::endl
                             << std::endl;
    }

    static void CaptureFrameIfRecordingEnabled(SDL_Renderer *renderer) {
        if (!SDLEventHelper::RECORDING_MODE) return;

        if (!frame_capture_initialized_) {
            if (!std::filesystem::exists(frame_directory_relative_path)) {
                std::filesystem::create_directory(frame_directory_relative_path);
            }

            int width = 0;
            int height = 0;
            SDL_GetRendererOutputSize(renderer, &width, &height);
            frame_capture_surface_ = SDL_CreateRGBSurfaceWithFormat( 0, width, height, 24, SDL_PIXELFORMAT_RGB24);
            FrameClock::Reset();
            frame_capture_initialized_ = true;
        }

        if (frame_capture_surface_ == nullptr) return;
        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24,
                                 frame_capture_surface_->pixels,
                                 frame_capture_surface_->pitch) != 0) {
            SDL_Log("SDL_RenderReadPixels() failed: %s", SDL_GetError());
            return;
        }

        std::stringstream filename_stream;
        filename_stream << "frame_" << std::setw(5) << std::setfill('0')
                        << FrameClock::GetFrameNumber() << ".bmp";
        const std::string output_file_path =
            frame_directory_relative_path + "/" + filename_stream.str();
        if (SDL_SaveBMP(frame_capture_surface_, output_file_path.c_str()) != 0) {
            SDL_Log("SDL_SaveBMP() failed: %s", SDL_GetError());
        }
    }
};

#endif
