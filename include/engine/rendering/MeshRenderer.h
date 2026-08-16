#ifndef MESH_RENDERER_H
#define MESH_RENDERER_H

#include <string>

struct Actor;

class MeshRenderer {
public:
    Actor *actor = nullptr;
    std::string key = "";
    bool enabled = true;

    // Phase 1 intentionally starts with one deterministic built-in mesh.
    std::string mesh = "builtin:cube";
    float color_r = 0.25f;
    float color_g = 0.65f;
    float color_b = 1.0f;
    float color_a = 1.0f;
};

#endif
