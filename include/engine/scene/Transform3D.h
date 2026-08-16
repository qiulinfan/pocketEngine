#ifndef TRANSFORM_3D_H
#define TRANSFORM_3D_H

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include <string>

struct Actor;

class Transform3D {
public:
    Actor *actor = nullptr;
    std::string key = "";
    bool enabled = true;

    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;

    // Quaternion component order is x, y, z, w in scene/Lua properties.
    float rotation_x = 0.0f;
    float rotation_y = 0.0f;
    float rotation_z = 0.0f;
    float rotation_w = 1.0f;

    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float scale_z = 1.0f;

    glm::vec3 Position() const;
    glm::quat Rotation() const;
    glm::vec3 Scale() const;
    glm::mat4 LocalMatrix() const;
    void NormalizeRotation();
};

#endif
