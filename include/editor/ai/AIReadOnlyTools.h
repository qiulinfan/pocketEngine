#ifndef EDITOR_AI_READ_ONLY_TOOLS_H
#define EDITOR_AI_READ_ONLY_TOOLS_H

#include "scene/Actor.h"
#include <filesystem>
#include <string>

class SceneDocument;

struct AIEditorContext {
    const SceneDocument *scene_document = nullptr;
    std::filesystem::path project_root;
    int selected_actor_index = -1;
    Actor::UID selected_runtime_actor_uid = Actor::kInvalidUID;
    bool play_mode_active = false;
    bool play_mode_paused = false;
    bool edit_mode_live_preview_enabled = false;
};

class AIReadOnlyTools {
public:
    static bool Execute(const std::string &tool,
                        const std::string &arguments_json,
                        const AIEditorContext &context,
                        std::string &out_result_json,
                        std::string &out_error_code,
                        std::string &out_error_message);
};

#endif
