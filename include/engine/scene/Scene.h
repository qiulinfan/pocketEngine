#ifndef SCENE_H
#define SCENE_H

#include "scene/Actor.h"
#include <filesystem>
#include <string>
#include <vector>

class Scene {
public:
    // Override the active scene subdirectory used for resource preference.
    // 覆盖当前活跃场景的子目录, 供资源优先级解析使用.
    static void SetActiveSceneSubdirectory( const std::filesystem::path &subdirectory);

    // load actors from <project-root>/scenes/<scene_name>.scene
    // 从 <project-root>/scenes/<scene_name>.scene 加载 actors
    static std::vector<Actor> LoadScene(const std::string &scene_name);

    // relative subdirectory under <project-root>/scenes for the active scene
    // 当前活跃 scene 在 <project-root>/scenes 下的相对子目录
    static const std::filesystem::path &GetActiveSceneSubdirectory();

private:
    static std::filesystem::path active_scene_subdirectory;
};

#endif
