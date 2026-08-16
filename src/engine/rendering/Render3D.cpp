#include "rendering/Render3D.h"
#include "rendering/MeshRenderer.h"
#include "scene/Camera3D.h"
#include "scene/Transform3D.h"
#include "scripting/ComponentManager.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace {

struct ComponentSnapshot {
    Actor::ComponentSpec spec;
    std::vector<Actor::ComponentProperty> properties;
};

using ComponentList = std::vector<ComponentSnapshot>;

const Actor::ComponentPropertyValue *FindProperty(
    const std::vector<Actor::ComponentProperty> &properties,
    const std::string &name) {
    const auto property_it = std::find_if(
        properties.begin(), properties.end(), [&](const auto &property) {
            return property.name == name;
        });
    return property_it == properties.end() ? nullptr : &property_it->value;
}

bool ReadBool(const std::vector<Actor::ComponentProperty> &properties,
              const std::string &name, bool fallback) {
    const auto *value = FindProperty(properties, name);
    if (value == nullptr) return fallback;
    if (const bool *typed = std::get_if<bool>(value)) return *typed;
    return fallback;
}

float ReadFloat(const std::vector<Actor::ComponentProperty> &properties,
                const std::string &name, float fallback) {
    const auto *value = FindProperty(properties, name);
    if (value == nullptr) return fallback;
    if (const double *typed = std::get_if<double>(value)) {
        return static_cast<float>(*typed);
    }
    if (const int *typed = std::get_if<int>(value)) {
        return static_cast<float>(*typed);
    }
    return fallback;
}

std::string ReadString(
    const std::vector<Actor::ComponentProperty> &properties,
    const std::string &name, const std::string &fallback) {
    const auto *value = FindProperty(properties, name);
    if (value == nullptr) return fallback;
    if (const std::string *typed = std::get_if<std::string>(value)) {
        return *typed;
    }
    return fallback;
}

void UpsertProperty(std::vector<Actor::ComponentProperty> &properties,
                    const Actor::ComponentProperty &override_property) {
    const auto property_it = std::find_if(
        properties.begin(), properties.end(), [&](const auto &property) {
            return property.name == override_property.name;
        });
    if (property_it == properties.end()) {
        properties.push_back(override_property);
    } else {
        property_it->value = override_property.value;
    }
}

ComponentList SnapshotComponents(const Actor &actor) {
    ComponentList components;
    std::vector<Actor::ComponentSpec> specs =
        ComponentManager::GetRuntimeComponentSpecs(actor.uid);
    const bool has_live_components = !specs.empty();
    if (!has_live_components) specs = actor.component_specs;

    components.reserve(specs.size());
    for (const Actor::ComponentSpec &spec : specs) {
        ComponentSnapshot snapshot;
        snapshot.spec = spec;
        snapshot.properties = has_live_components
                                  ? ComponentManager::GetRuntimeComponentProperties(
                                        actor.uid, spec.key)
                                  : ComponentManager::GetComponentTypeDefaultProperties(
                                        spec.type);
        if (!has_live_components) {
            for (const Actor::ComponentProperty &property : spec.overrides) {
                UpsertProperty(snapshot.properties, property);
            }
        }
        components.emplace_back(std::move(snapshot));
    }
    std::sort(components.begin(), components.end(),
              [](const ComponentSnapshot &a, const ComponentSnapshot &b) {
                  return a.spec.key < b.spec.key;
              });
    return components;
}

const ComponentSnapshot *FindSingleComponent(const ComponentList &components,
                                             const std::string &type,
                                             bool &out_duplicate) {
    const ComponentSnapshot *match = nullptr;
    out_duplicate = false;
    for (const ComponentSnapshot &component : components) {
        if (component.spec.type != type) continue;
        if (match != nullptr) {
            out_duplicate = true;
            return match;
        }
        match = &component;
    }
    return match;
}

Transform3D MakeTransform(const ComponentSnapshot &component) {
    Transform3D transform;
    transform.enabled = ReadBool(component.properties, "enabled", true);
    transform.position_x =
        ReadFloat(component.properties, "position_x", transform.position_x);
    transform.position_y =
        ReadFloat(component.properties, "position_y", transform.position_y);
    transform.position_z =
        ReadFloat(component.properties, "position_z", transform.position_z);
    transform.rotation_x =
        ReadFloat(component.properties, "rotation_x", transform.rotation_x);
    transform.rotation_y =
        ReadFloat(component.properties, "rotation_y", transform.rotation_y);
    transform.rotation_z =
        ReadFloat(component.properties, "rotation_z", transform.rotation_z);
    transform.rotation_w =
        ReadFloat(component.properties, "rotation_w", transform.rotation_w);
    transform.scale_x =
        ReadFloat(component.properties, "scale_x", transform.scale_x);
    transform.scale_y =
        ReadFloat(component.properties, "scale_y", transform.scale_y);
    transform.scale_z =
        ReadFloat(component.properties, "scale_z", transform.scale_z);
    return transform;
}

Camera3D MakeCamera(const ComponentSnapshot &component) {
    Camera3D camera;
    camera.enabled = ReadBool(component.properties, "enabled", true);
    camera.primary = ReadBool(component.properties, "primary", true);
    camera.orthographic =
        ReadBool(component.properties, "orthographic", false);
    camera.vertical_fov_degrees = ReadFloat(
        component.properties, "vertical_fov_degrees",
        camera.vertical_fov_degrees);
    camera.near_clip =
        ReadFloat(component.properties, "near_clip", camera.near_clip);
    camera.far_clip =
        ReadFloat(component.properties, "far_clip", camera.far_clip);
    camera.orthographic_height = ReadFloat(
        component.properties, "orthographic_height",
        camera.orthographic_height);
    return camera;
}

MeshRenderer MakeMeshRenderer(const ComponentSnapshot &component) {
    MeshRenderer mesh_renderer;
    mesh_renderer.enabled = ReadBool(component.properties, "enabled", true);
    mesh_renderer.mesh =
        ReadString(component.properties, "mesh", mesh_renderer.mesh);
    mesh_renderer.color_r =
        ReadFloat(component.properties, "color_r", mesh_renderer.color_r);
    mesh_renderer.color_g =
        ReadFloat(component.properties, "color_g", mesh_renderer.color_g);
    mesh_renderer.color_b =
        ReadFloat(component.properties, "color_b", mesh_renderer.color_b);
    mesh_renderer.color_a =
        ReadFloat(component.properties, "color_a", mesh_renderer.color_a);
    return mesh_renderer;
}

bool Fail(std::string *out_error, const std::string &message) {
    if (out_error != nullptr) *out_error = message;
    return false;
}

} // namespace

bool ExtractRenderFrame3D(const std::deque<Actor> &actors, int width,
                          int height, RenderFrame3D &out_frame,
                          std::string *out_error) {
    out_frame = RenderFrame3D{};
    if (width <= 0 || height <= 0) {
        return Fail(out_error, "render3d.invalid_viewport");
    }

    std::unordered_map<Actor::UID, const Actor *> actor_by_uid;
    std::unordered_map<Actor::UID, ComponentList> components_by_actor;
    actor_by_uid.reserve(actors.size());
    components_by_actor.reserve(actors.size());
    for (const Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        actor_by_uid[actor.uid] = &actor;
        components_by_actor.emplace(actor.uid, SnapshotComponents(actor));
    }

    std::unordered_map<Actor::UID, glm::mat4> world_by_actor;
    std::unordered_set<Actor::UID> resolving;
    std::function<bool(Actor::UID)> resolve_world = [&](Actor::UID uid) {
        if (world_by_actor.find(uid) != world_by_actor.end()) return true;
        const auto actor_it = actor_by_uid.find(uid);
        if (actor_it == actor_by_uid.end()) return false;
        if (!resolving.insert(uid).second) {
            return Fail(out_error, "transform3d.hierarchy_cycle");
        }

        const ComponentList &components = components_by_actor.at(uid);
        bool duplicate_3d = false;
        const ComponentSnapshot *transform_component =
            FindSingleComponent(components, "Transform3D", duplicate_3d);
        bool duplicate_2d = false;
        const ComponentSnapshot *transform_2d =
            FindSingleComponent(components, "Transform", duplicate_2d);
        if (duplicate_3d || duplicate_2d ||
            (transform_component != nullptr && transform_2d != nullptr)) {
            resolving.erase(uid);
            return Fail(out_error, "transform3d.ambiguous_transform");
        }

        glm::mat4 world(1.0f);
        if (transform_component != nullptr) {
            const Transform3D transform = MakeTransform(*transform_component);
            if (transform.enabled) world = transform.LocalMatrix();
        }

        const Actor *actor = actor_it->second;
        if (actor->parent_uid != Actor::kInvalidUID) {
            const auto parent_it = actor_by_uid.find(actor->parent_uid);
            if (parent_it == actor_by_uid.end()) {
                resolving.erase(uid);
                return Fail(out_error, "transform3d.parent_missing");
            }
            const ComponentList &parent_components =
                components_by_actor.at(actor->parent_uid);
            bool ignored_duplicate = false;
            const bool parent_has_2d =
                FindSingleComponent(parent_components, "Transform",
                                    ignored_duplicate) != nullptr;
            if (transform_component != nullptr && parent_has_2d) {
                resolving.erase(uid);
                return Fail(out_error, "transform3d.cross_dimension_parent");
            }
            if (!resolve_world(actor->parent_uid)) {
                resolving.erase(uid);
                return false;
            }
            world = world_by_actor.at(actor->parent_uid) * world;
        }

        resolving.erase(uid);
        world_by_actor[uid] = world;
        return true;
    };

    for (const Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        if (!resolve_world(actor.uid)) return false;
    }

    const float aspect_ratio = static_cast<float>(width) /
                               static_cast<float>(height);
    bool found_primary_camera = false;
    for (const Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        const ComponentList &components = components_by_actor.at(actor.uid);
        for (const ComponentSnapshot &component : components) {
            if (component.spec.type != "Camera3D") continue;
            const Camera3D camera = MakeCamera(component);
            if (!camera.enabled || !camera.primary) continue;
            if (found_primary_camera) {
                return Fail(out_error, "camera.multiple_primary");
            }
            std::string camera_error;
            if (!camera.Validate(aspect_ratio, &camera_error)) {
                return Fail(out_error, camera_error);
            }
            found_primary_camera = true;
            out_frame.view.camera_actor_uid = actor.uid;
            out_frame.view.view = glm::inverse(world_by_actor.at(actor.uid));
            out_frame.view.projection = camera.ProjectionMatrix(aspect_ratio);
        }
    }
    if (!found_primary_camera) {
        return Fail(out_error, "camera.primary_missing");
    }

    for (const Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        const ComponentList &components = components_by_actor.at(actor.uid);
        bool ignored_duplicate = false;
        const ComponentSnapshot *transform_component =
            FindSingleComponent(components, "Transform3D", ignored_duplicate);
        if (transform_component == nullptr) continue;
        for (const ComponentSnapshot &component : components) {
            if (component.spec.type != "MeshRenderer") continue;
            const MeshRenderer renderer = MakeMeshRenderer(component);
            if (!renderer.enabled) continue;
            MeshDrawCommand3D command;
            command.actor_uid = actor.uid;
            command.component_key = component.spec.key;
            command.mesh = renderer.mesh;
            command.world = world_by_actor.at(actor.uid);
            command.color = glm::clamp(
                glm::vec4(renderer.color_r, renderer.color_g,
                          renderer.color_b, renderer.color_a),
                glm::vec4(0.0f), glm::vec4(1.0f));
            out_frame.draws.emplace_back(std::move(command));
        }
    }

    if (out_error != nullptr) out_error->clear();
    return true;
}
