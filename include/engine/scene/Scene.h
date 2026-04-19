#ifndef SCENE_H
#define SCENE_H

#include "scene/Actor.h"
#include <filesystem>
#include <string>
#include <vector>

class Scene {
public:
    // allocate a globally unique actor id
    // 分配一个全局唯一的 actor id
    static int AllocateActorID();

    // Override the active scene subdirectory used for resource preference.
    // 覆盖当前活跃场景的子目录, 供资源优先级解析使用.
    static void SetActiveSceneSubdirectory( const std::filesystem::path &subdirectory);

    // load actors from resources/scenes/<scene_name>.scene
    // 从 resources/scenes/<scene_name>.scene 加载 actors
    static std::vector<Actor> LoadScene(const std::string &scene_name);

    // relative subdirectory under resources/scenes for the currently active scene
    // 当前活跃 scene 在 resources/scenes 下的相对子目录
    static const std::filesystem::path &GetActiveSceneSubdirectory();

private:
    // next actor id counter
    // 下一个 actor id 计数器
    static int next_actor_id;
    static std::filesystem::path active_scene_subdirectory;
};

#endif
