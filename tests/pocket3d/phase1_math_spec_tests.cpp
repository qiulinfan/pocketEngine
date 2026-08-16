#include "TestSupport.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtc/quaternion.hpp"
#include <algorithm>
#include <iostream>
#include <string>

namespace {

constexpr float kTolerance = 0.0001f;

void ExpectVec3Near(Pocket3DTestContext &context, const glm::vec3 &actual,
                    const glm::vec3 &expected, const std::string &case_id) {
    context.ExpectNear(actual.x, expected.x, kTolerance, case_id + " x");
    context.ExpectNear(actual.y, expected.y, kTolerance, case_id + " y");
    context.ExpectNear(actual.z, expected.z, kTolerance, case_id + " z");
}

glm::vec3 TransformPoint(const glm::mat4 &matrix, const glm::vec3 &point) {
    const glm::vec4 transformed = matrix * glm::vec4(point, 1.0f);
    return glm::vec3(transformed) / transformed.w;
}

glm::vec3 UnprojectNdc(const glm::mat4 &inverse_view_projection,
                       const glm::vec3 &ndc) {
    const glm::vec4 world =
        inverse_view_projection * glm::vec4(ndc, 1.0f);
    return glm::vec3(world) / world.w;
}

glm::vec2 ProjectToNdc(const glm::mat4 &view_projection,
                       const glm::vec3 &world) {
    const glm::vec4 clip = view_projection * glm::vec4(world, 1.0f);
    return glm::vec2(clip) / clip.w;
}

void TestRightHandedBasisAndQuaternion(Pocket3DTestContext &context) {
    const glm::vec3 cross =
        glm::cross(glm::vec3(1.0f, 0.0f, 0.0f),
                   glm::vec3(0.0f, 1.0f, 0.0f));
    ExpectVec3Near(context, cross, glm::vec3(0.0f, 0.0f, 1.0f),
                   "P1-MATH-001 right-handed basis");

    const glm::quat rotate_y = glm::angleAxis(
        glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 rotated_x = rotate_y * glm::vec3(1.0f, 0.0f, 0.0f);
    ExpectVec3Near(context, rotated_x, glm::vec3(0.0f, 0.0f, -1.0f),
                   "P1-MATH-001 positive Y rotation");
}

void TestParentTrsOrder(Pocket3DTestContext &context) {
    const glm::quat parent_rotation = glm::angleAxis(
        glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 parent =
        glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f)) *
        glm::mat4_cast(parent_rotation) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    const glm::mat4 child =
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 child_world =
        TransformPoint(parent * child, glm::vec3(0.0f));
    ExpectVec3Near(context, child_world, glm::vec3(10.0f, 0.0f, -2.0f),
                   "P1-MATH-002 T*R*S hierarchy");
}

void TestRightHandedZeroToOneDepth(Pocket3DTestContext &context) {
    constexpr float near_clip = 0.1f;
    constexpr float far_clip = 100.0f;
    const glm::mat4 projection = glm::perspectiveRH_ZO(
        glm::radians(90.0f), 1.0f, near_clip, far_clip);

    const glm::vec4 near_clip_position =
        projection * glm::vec4(0.0f, 0.0f, -near_clip, 1.0f);
    const glm::vec4 far_clip_position =
        projection * glm::vec4(0.0f, 0.0f, -far_clip, 1.0f);
    const float near_ndc = near_clip_position.z / near_clip_position.w;
    const float far_ndc = far_clip_position.z / far_clip_position.w;

    context.ExpectNear(near_ndc, 0.0f, kTolerance,
                       "P1-MATH-003 near plane maps to zero");
    context.ExpectNear(far_ndc, 1.0f, kTolerance,
                       "P1-MATH-003 far plane maps to one");
}

void TestScreenCenterRay(Pocket3DTestContext &context) {
    const glm::mat4 view = glm::lookAtRH(
        glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection =
        glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    const glm::mat4 inverse_view_projection =
        glm::inverse(projection * view);
    const glm::vec3 near_world =
        UnprojectNdc(inverse_view_projection, glm::vec3(0.0f, 0.0f, 0.0f));
    const glm::vec3 far_world =
        UnprojectNdc(inverse_view_projection, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 direction = glm::normalize(far_world - near_world);
    ExpectVec3Near(context, direction, glm::vec3(0.0f, 0.0f, -1.0f),
                   "P1-MATH-004 center ray");
}

void TestCounterClockwiseFrontFace(Pocket3DTestContext &context) {
    const glm::mat4 view = glm::lookAtRH(
        glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection =
        glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    const glm::mat4 view_projection = projection * view;

    const glm::vec2 a = ProjectToNdc(
        view_projection, glm::vec3(-0.5f, -0.5f, -1.0f));
    const glm::vec2 b = ProjectToNdc(
        view_projection, glm::vec3(0.5f, -0.5f, -1.0f));
    const glm::vec2 c = ProjectToNdc(
        view_projection, glm::vec3(0.0f, 0.5f, -1.0f));
    const float signed_twice_area =
        (b.x - a.x) * (c.y - a.y) -
        (b.y - a.y) * (c.x - a.x);
    context.Expect(signed_twice_area > 0.0f,
                   "P1-MATH-005 projected front face must be CCW");
}

} // namespace

int main() {
    Pocket3DTestContext context;
    TestRightHandedBasisAndQuaternion(context);
    TestParentTrsOrder(context);
    TestRightHandedZeroToOneDepth(context);
    TestScreenCenterRay(context);
    TestCounterClockwiseFrontFace(context);
    if (context.FailureCount() != 0) return 1;
    std::cout << "Pocket3D Phase 1 math specification tests passed."
              << std::endl;
    return 0;
}
