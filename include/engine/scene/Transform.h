#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <string>

struct Actor;

class Transform {
public:
    Transform() = default;
    Transform(const Transform &) = default;

    /*
    Built-in components keep the same common surface as the existing native
    components so Lua, inspector, and runtime hot-editing can treat them uniformly.
    */
    Actor *actor = nullptr;
    std::string key = "";
    bool enabled = true;

    // local pose relative to parent actor. for root actors this is the world pose.
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;

    /*re
    Derived world-space values are resolved from the parent chain at runtime
    and are not serialized back into scenes.
    */
    float world_x = 0.0f;
    float world_y = 0.0f;
    float world_rotation = 0.0f;
};

#endif
