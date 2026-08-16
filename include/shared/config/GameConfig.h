#ifndef SHARED_GAME_CONFIG_H
#define SHARED_GAME_CONFIG_H

#include <string>

enum class RenderingMode {
    TwoD,
    ThreeD,
};

struct GameConfigData {
    // Runtime window resolution and clear-color defaults used by both the
    // standalone runtime app and the editor-hosted runtime viewport.
    int window_width = 640;
    int window_height = 360;

    int clear_color_r = 255;
    int clear_color_g = 255;
    int clear_color_b = 255;

    float zoom_factor = 1.0f;
    RenderingMode rendering_mode = RenderingMode::TwoD;
    bool vsync = true;
    bool valid = true;
    std::string error_code;

    std::string game_title = "";
    std::string initial_scene_name = "";
};

class GameConfig {
public:
    static bool TryParseRenderingMode(const std::string &value,
                                      RenderingMode &out_mode);
    static const char *RenderingModeName(RenderingMode mode);
    // Read game.config and rendering.config into one lightweight shared value
    // object. Engine owns the behavior; this type only describes startup data.
    static GameConfigData Read();
    // Persist project startup config immediately back into game.config and
    // rendering.config. This is used by the editor settings UI and does not
    // hot-reload the currently running runtime instance.
    static bool Write(const GameConfigData &config);
};

#endif
