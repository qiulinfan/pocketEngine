#include "TestSupport.h"
#include "rendering/Render3D.h"
#include "scene/Camera3D.h"
#include "scene/Transform3D.h"
#include "glm/gtc/matrix_transform.hpp"
#include <cmath>
#include <iostream>
#include <string>

namespace {

Actor::ComponentProperty Property(
    const std::string &name, Actor::ComponentPropertyValue value) {
    return Actor::ComponentProperty{name, std::move(value)};
}

Actor::ComponentSpec Component(
    const std::string &key, const std::string &type,
    std::vector<Actor::ComponentProperty> properties = {}) {
    Actor::ComponentSpec component;
    component.key = key;
    component.type = type;
    component.overrides = std::move(properties);
    return component;
}

void TestTransform3D(Pocket3DTestContext &context) {
    Transform3D transform;
    const glm::mat4 identity(1.0f);
    const glm::mat4 local = transform.LocalMatrix();
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            context.ExpectNear(local[column][row], identity[column][row],
                               0.0001f,
                               "P1-TRS-001 default identity matrix");
        }
    }

    transform.rotation_x = 0.0f;
    transform.rotation_y = 2.0f;
    transform.rotation_z = 0.0f;
    transform.rotation_w = 2.0f;
    transform.NormalizeRotation();
    const float quaternion_length = std::sqrt(
        transform.rotation_x * transform.rotation_x +
        transform.rotation_y * transform.rotation_y +
        transform.rotation_z * transform.rotation_z +
        transform.rotation_w * transform.rotation_w);
    context.ExpectNear(quaternion_length, 1.0f, 0.0001f,
                       "P1-TRS-001 quaternion normalization");

    Transform3D parent;
    parent.position_x = 10.0f;
    parent.rotation_y = std::sin(glm::radians(45.0f));
    parent.rotation_w = std::cos(glm::radians(45.0f));
    parent.scale_x = parent.scale_y = parent.scale_z = 2.0f;
    Transform3D child;
    child.position_x = 1.0f;
    const glm::vec4 child_origin =
        parent.LocalMatrix() * child.LocalMatrix() * glm::vec4(0, 0, 0, 1);
    context.ExpectNear(child_origin.x, 10.0f, 0.0001f,
                       "P1-TRS-002 hierarchy x");
    context.ExpectNear(child_origin.z, -2.0f, 0.0001f,
                       "P1-TRS-002 hierarchy z");
}

void TestCamera3D(Pocket3DTestContext &context) {
    Camera3D camera;
    std::string error;
    context.Expect(camera.Validate(16.0f / 9.0f, &error),
                   "P1-CAM-001 default perspective must be valid");
    const glm::mat4 projection = camera.ProjectionMatrix(1.0f);
    const glm::vec4 near_clip =
        projection * glm::vec4(0, 0, -camera.near_clip, 1);
    const glm::vec4 far_clip =
        projection * glm::vec4(0, 0, -camera.far_clip, 1);
    context.ExpectNear(near_clip.z / near_clip.w, 0.0f, 0.0001f,
                       "P1-CAM-001 near depth");
    context.ExpectNear(far_clip.z / far_clip.w, 1.0f, 0.0001f,
                       "P1-CAM-001 far depth");

    camera.near_clip = 0.0f;
    context.Expect(!camera.Validate(1.0f, &error) &&
                       error == "camera.invalid_clip_planes",
                   "P1-CAM-001 invalid clip planes must be rejected");
}

void TestExtraction(Pocket3DTestContext &context) {
    std::deque<Actor> actors;

    Actor camera;
    camera.uid = 1;
    camera.component_specs = {
        Component("camera", "Camera3D"),
        Component("transform", "Transform3D",
                  {Property("position_z", 5.0)})};
    actors.push_back(camera);

    Actor parent;
    parent.uid = 2;
    parent.component_specs = {
        Component("mesh", "MeshRenderer",
                  {Property("color_r", 0.2), Property("color_g", 0.6)}),
        Component("transform", "Transform3D",
                  {Property("position_x", 1.0),
                   Property("scale_x", 2.0), Property("scale_y", 2.0),
                   Property("scale_z", 2.0)})};
    actors.push_back(parent);

    Actor child;
    child.uid = 3;
    child.parent_uid = 2;
    child.component_specs = {
        Component("mesh", "MeshRenderer"),
        Component("transform", "Transform3D",
                  {Property("position_x", 1.0)})};
    actors.push_back(child);

    RenderFrame3D frame;
    std::string error;
    context.Expect(ExtractRenderFrame3D(actors, 640, 360, frame, &error),
                   "P1-EXT-001 extraction must succeed: " + error);
    context.Expect(frame.view.camera_actor_uid == 1,
                   "P1-CAM-002 stable primary camera selection");
    context.Expect(frame.draws.size() == 2,
                   "P1-EXT-001 expected two draw commands");
    if (frame.draws.size() == 2) {
        context.Expect(frame.draws[0].actor_uid == 2 &&
                           frame.draws[1].actor_uid == 3,
                       "P1-EXT-001 commands preserve actor/component order");
        context.ExpectNear(frame.draws[1].world[3].x, 3.0f, 0.0001f,
                           "P1-DRAW-002 parent world transform");
        context.Expect(frame.draws[0].mesh == "builtin:cube",
                       "P1-DRAW-001 built-in indexed cube request");
    }

    actors[1].component_specs.push_back(Component("legacy", "Transform"));
    context.Expect(!ExtractRenderFrame3D(actors, 640, 360, frame, &error) &&
                       error == "transform3d.ambiguous_transform",
                   "P1-TRS-004 mixed transforms must be rejected");
}

} // namespace

int main() {
    Pocket3DTestContext context;
    TestTransform3D(context);
    TestCamera3D(context);
    TestExtraction(context);
    if (context.FailureCount() != 0) return 1;
    std::cout << "Pocket3D Transform3D, Camera3D, MeshRenderer and extraction "
                 "contracts passed."
              << std::endl;
    return 0;
}
