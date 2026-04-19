#ifndef ENGINE_INPUT_SDL_EVENT_HELPER_H
#define ENGINE_INPUT_SDL_EVENT_HELPER_H

#include "core/FrameClock.h"
#include "SDL2/SDL.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

class SDLEventHelper {
public:
    // Turn RECORDING_MODE on to record inputs as you play.
    // 打开 RECORDING_MODE 可记录当前会话的输入事件.
    inline static const bool RECORDING_MODE = false;
    inline static const char *USER_INPUT_FILENAME = "sdl_user_input.txt";

    enum class InputStatus {
        NotInitialized,
        InputFileMissing,
        InputFilePresent
    };

    // Wrapper that can inject recorded input events into SDL queue.
    // 这个包装层会在有输入文件时向 SDL 队列注入回放事件.
    static int SDL_PollEvent(SDL_Event *event) {
        SDL_ConsiderInputFile();
        return ::SDL_PollEvent(event);
    }

    static bool HasInitializedPolling() {
        return input_status_ != InputStatus::NotInitialized;
    }

    static InputStatus GetInputStatus() {
        return input_status_;
    }

private:
    inline static std::unordered_map<int, std::queue<SDL_Event>>
        frame_to_user_input_;
    inline static InputStatus input_status_ = InputStatus::NotInitialized;
    inline static std::ofstream recording_file_;

    static void SDL_ConsiderInputFile() {
        // Lazy initialize replay input cache.
        if (input_status_ == InputStatus::NotInitialized) {
            LoadSDLEventsFromInputFile();
        }

        if (input_status_ == InputStatus::InputFilePresent) {
            const int frame_number = FrameClock::GetFrameNumber();
            auto frame_it = frame_to_user_input_.find(frame_number);
            if (frame_it != frame_to_user_input_.end()) {
                while (!frame_it->second.empty()) {
                    SDL_PushEvent(&(frame_it->second.front()));
                    frame_it->second.pop();
                }
            }
        }

        // Optional recording mode (kept for debugging workflows).
        if (RECORDING_MODE && input_status_ != InputStatus::InputFilePresent) {
            SDL_PumpEvents();
            SDL_Event incoming_events[100];
            const int num_events = SDL_PeepEvents(incoming_events, 100, SDL_PEEKEVENT,
                                                  SDL_FIRSTEVENT, SDL_LASTEVENT);

            std::vector<SDL_Event> relevant_events;
            for (int i = 0; i < num_events; ++i) {
                const Uint32 event_type = incoming_events[i].type;
                if (event_type == SDL_KEYUP || event_type == SDL_KEYDOWN ||
                    event_type == SDL_MOUSEMOTION ||
                    event_type == SDL_MOUSEBUTTONDOWN ||
                    event_type == SDL_MOUSEBUTTONUP ||
                    event_type == SDL_MOUSEWHEEL || event_type == SDL_QUIT) {
                    relevant_events.push_back(incoming_events[i]);
                }
            }

            if (relevant_events.empty()) return;
            if (!recording_file_.is_open()) {
                recording_file_.open("recorded_sdl_user_input.txt");
            }

            recording_file_ << FrameClock::GetFrameNumber() << ";";
            for (size_t i = 0; i < relevant_events.size(); ++i) {
                const Uint32 event_type = relevant_events[i].type;
                recording_file_ << event_type << ",";

                if (event_type == SDL_KEYDOWN || event_type == SDL_KEYUP) {
                    const SDL_Scancode keycode = relevant_events[i].key.keysym.scancode;
                    recording_file_ << keycode;
                } else if (event_type == SDL_MOUSEMOTION) {
                    const Sint32 x = relevant_events[i].motion.x;
                    const Sint32 y = relevant_events[i].motion.y;
                    recording_file_ << x << "," << y;
                } else if (event_type == SDL_MOUSEBUTTONDOWN ||
                           event_type == SDL_MOUSEBUTTONUP) {
                               const int button_index = static_cast<int>(relevant_events[i].button.button);
                    recording_file_ << button_index;
                } else if (event_type == SDL_MOUSEWHEEL) {
                    const float scroll_amount = relevant_events[i].wheel.preciseY;
                    recording_file_ << scroll_amount;
                }

                recording_file_ << ";";
            }
            recording_file_ << std::endl;
        }
    }

    static void LoadSDLEventsFromInputFile() {
        frame_to_user_input_.clear();

        if (!std::filesystem::exists(USER_INPUT_FILENAME)) {
            input_status_ = InputStatus::InputFileMissing;
            return;
        }

        std::ifstream infile(USER_INPUT_FILENAME);
        std::string line;

        while (std::getline(infile, line)) {
            std::istringstream line_stream(line);
            std::string event_string;
            std::string frame_string;

            std::getline(line_stream, frame_string, ';');
            if (frame_string.empty()) continue;
            const int frame_number = std::stoi(frame_string);

            std::queue<SDL_Event> &events_queue = frame_to_user_input_[frame_number];

            while (std::getline(line_stream, event_string, ';')) {
                std::istringstream event_stream(event_string);
                std::string event_type_string;
                std::getline(event_stream, event_type_string, ',');

                event_type_string.erase(
                    std::remove(event_type_string.begin(), event_type_string.end(),
                                '\r'),
                    event_type_string.end());
                if (event_type_string.empty()) continue;

                const Uint32 event_type = static_cast<Uint32>(std::stoi(event_type_string));
                SDL_Event fabricated_sdl_event;
                fabricated_sdl_event.type = event_type;

                if (event_type == SDL_KEYUP || event_type == SDL_KEYDOWN) {
                    std::string keycode;
                    std::getline(event_stream, keycode, ',');
                    if (keycode.empty()) continue;
                    fabricated_sdl_event.key.keysym.scancode = static_cast<SDL_Scancode>(std::stoi(keycode));
                } else if (event_type == SDL_MOUSEMOTION) {
                    std::string x_string;
                    std::string y_string;
                    std::getline(event_stream, x_string, ',');
                    std::getline(event_stream, y_string, ',');
                    if (x_string.empty() || y_string.empty()) continue;
                    fabricated_sdl_event.motion.x = static_cast<Sint32>(std::stoi(x_string));
                    fabricated_sdl_event.motion.y = static_cast<Sint32>(std::stoi(y_string));
                } else if (event_type == SDL_MOUSEBUTTONDOWN ||
                           event_type == SDL_MOUSEBUTTONUP) {
                    std::string mouse_button_index_string;
                    std::getline(event_stream, mouse_button_index_string, ',');
                    if (mouse_button_index_string.empty()) continue;
                    fabricated_sdl_event.button.button = static_cast<Uint8>(std::stoi(mouse_button_index_string));
                } else if (event_type == SDL_MOUSEWHEEL) {
                    std::string mouse_wheel_string;
                    std::getline(event_stream, mouse_wheel_string, ',');
                    if (mouse_wheel_string.empty()) continue;
                    fabricated_sdl_event.wheel.preciseY = std::stof(mouse_wheel_string);
                }

                events_queue.push(fabricated_sdl_event);
            }
        }

        input_status_ = InputStatus::InputFilePresent;
    }
};

#endif
