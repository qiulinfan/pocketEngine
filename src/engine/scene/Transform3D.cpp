#include "scene/Transform3D.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"
#include <cmath>

glm::vec3 Transform3D::Position() const {
    return glm::vec3(position_x, position_y, position_z);
}

glm::quat Transform3D::Rotation() const {
    glm::quat rotation(rotation_w, rotation_x, rotation_y, rotation_z);
    const float length_squared = glm::dot(rotation, rotation);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-12f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return glm::normalize(rotation);
}

glm::vec3 Transform3D::Scale() const {
    return glm::vec3(scale_x, scale_y, scale_z);
}

glm::mat4 Transform3D::LocalMatrix() const {
    return glm::translate(glm::mat4(1.0f), Position()) *
           glm::toMat4(Rotation()) * glm::scale(glm::mat4(1.0f), Scale());
}

void Transform3D::NormalizeRotation() {
    const glm::quat rotation = Rotation();
    rotation_x = rotation.x;
    rotation_y = rotation.y;
    rotation_z = rotation.z;
    rotation_w = rotation.w;
}
