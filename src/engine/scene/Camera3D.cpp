#include "scene/Camera3D.h"
#include "glm/ext/matrix_clip_space.hpp"
#include <cmath>

bool Camera3D::Validate(float aspect_ratio, std::string *out_error) const {
    auto fail = [&](const char *message) {
        if (out_error != nullptr) *out_error = message;
        return false;
    };
    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0.0f) {
        return fail("camera.invalid_aspect_ratio");
    }
    if (!std::isfinite(near_clip) || !std::isfinite(far_clip) ||
        near_clip <= 0.0f || far_clip <= near_clip) {
        return fail("camera.invalid_clip_planes");
    }
    if (orthographic) {
        if (!std::isfinite(orthographic_height) ||
            orthographic_height <= 0.0f) {
            return fail("camera.invalid_orthographic_height");
        }
    } else if (!std::isfinite(vertical_fov_degrees) ||
               vertical_fov_degrees <= 1.0f ||
               vertical_fov_degrees >= 179.0f) {
        return fail("camera.invalid_vertical_fov");
    }
    if (out_error != nullptr) out_error->clear();
    return true;
}

glm::mat4 Camera3D::ProjectionMatrix(float aspect_ratio) const {
    if (!Validate(aspect_ratio, nullptr)) return glm::mat4(1.0f);
    if (orthographic) {
        const float half_height = orthographic_height * 0.5f;
        const float half_width = half_height * aspect_ratio;
        return glm::orthoRH_ZO(-half_width, half_width, -half_height,
                              half_height, near_clip, far_clip);
    }
    return glm::perspectiveRH_ZO(glm::radians(vertical_fov_degrees),
                                 aspect_ratio, near_clip, far_clip);
}
