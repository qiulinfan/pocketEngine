#ifndef CAMERA_3D_H
#define CAMERA_3D_H

#include "glm/glm.hpp"
#include <string>

struct Actor;

class Camera3D {
public:
    Actor *actor = nullptr;
    std::string key = "";
    bool enabled = true;
    bool primary = true;
    bool orthographic = false;

    float vertical_fov_degrees = 60.0f;
    float near_clip = 0.1f;
    float far_clip = 1000.0f;
    float orthographic_height = 10.0f;

    bool Validate(float aspect_ratio, std::string *out_error = nullptr) const;
    glm::mat4 ProjectionMatrix(float aspect_ratio) const;
};

#endif
