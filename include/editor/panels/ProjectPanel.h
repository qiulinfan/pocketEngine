#ifndef EDITOR_PROJECT_PANEL_H
#define EDITOR_PROJECT_PANEL_H

#include "editor/core/EditorConfig.h"
#include <filesystem>

struct SDL_Renderer;

namespace EditorPanels {

struct ProjectPanelResult {
    bool open_scene_requested = false;
    std::filesystem::path requested_scene_path;
    bool open_external_editor_requested = false;
    EditorExternalFileType requested_external_file_type =
        EditorExternalFileType::Generic;
    std::filesystem::path requested_external_file_path;
    std::filesystem::path requested_external_resources_root;
};

// Render a read-only project browser tree for the resources directory.
// 渲染 resources 目录的只读工程浏览树.
ProjectPanelResult RenderProjectPanel(
    const std::filesystem::path &resources_root, SDL_Renderer *renderer);

} // namespace EditorPanels

#endif
