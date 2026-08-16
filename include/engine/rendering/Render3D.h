#ifndef RENDER_3D_H
#define RENDER_3D_H

#include "glm/glm.hpp"
#include "scene/Actor.h"
#include <deque>
#include <string>
#include <vector>

struct RenderView3D {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    Actor::UID camera_actor_uid = Actor::kInvalidUID;
};

struct MeshDrawCommand3D {
    Actor::UID actor_uid = Actor::kInvalidUID;
    std::string component_key;
    std::string mesh = "builtin:cube";
    glm::mat4 world = glm::mat4(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
};

struct RenderFrame3D {
    RenderView3D view;
    std::vector<MeshDrawCommand3D> draws;
};

// Extracts a pointer-free, stable render snapshot from the live component
// world. OpenGL is deliberately absent from this boundary.
bool ExtractRenderFrame3D(const std::deque<Actor> &actors, int width,
                          int height, RenderFrame3D &out_frame,
                          std::string *out_error = nullptr);

#endif
