#include "editor/panels/ScenePanel.h"
#include "editor/core/EditorDragDrop.h"
#include "editor/documents/SceneDocument.h"
#include "engine/core/Engine.h"
#include "scripting/ComponentManager.h"
#include "imgui.h"
#include "SDL2/SDL.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>

namespace EditorPanels {
namespace {

constexpr float kPixelsPerUnit = 100.0f;
constexpr float kSelectionStrokeThickness = 2.0f;
constexpr float kSelectionPaddingPixels = 6.0f;
constexpr float kFallbackSelectionHalfExtentUnits = 0.45f;

struct ActorScreenBounds {
    std::size_t actor_index = std::numeric_limits<std::size_t>::max();
    int runtime_actor_id = -1;
    std::uint64_t editor_actor_uid = Actor::kInvalidEditorActorUID;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float half_width = 0.0f;
    float half_height = 0.0f;
    bool circular = false;
};

struct ActorDragTarget {
    std::size_t actor_index = std::numeric_limits<std::size_t>::max();
    int runtime_actor_id = -1;
    std::uint64_t editor_actor_uid = Actor::kInvalidEditorActorUID;
    std::string component_key;
    float world_x = 0.0f;
    float world_y = 0.0f;
    bool transform_component = false;
};

struct SceneDragState {
    bool active = false;
    bool play_mode_active = false;
    std::size_t actor_index = std::numeric_limits<std::size_t>::max();
    int runtime_actor_id = -1;
    std::uint64_t editor_actor_uid = Actor::kInvalidEditorActorUID;
    std::string component_key;
    float world_offset_x = 0.0f;
    float world_offset_y = 0.0f;
};

SceneDragState g_scene_drag_state;

struct SceneViewCameraState {
    bool initialized = false;
    std::string scene_key;
    float world_x = 0.0f;
    float world_y = 0.0f;
    float zoom = 1.0f;
};

SceneViewCameraState g_scene_camera_state;

ImVec2 FitPreviewImage(const ImVec2 &available_size, int texture_width,
                       int texture_height) {
    const float safe_texture_width =
        static_cast<float>(std::max(1, texture_width));
    const float safe_texture_height =
        static_cast<float>(std::max(1, texture_height));
    const float width_scale = available_size.x / safe_texture_width;
    const float height_scale = available_size.y / safe_texture_height;
    const float image_scale =
        std::max(0.0f, std::min(width_scale, height_scale));
    return ImVec2(safe_texture_width * image_scale,
                  safe_texture_height * image_scale);
}

float ClampSceneCameraZoom(float zoom) {
    return std::clamp(zoom, 0.1f, 8.0f);
}

std::string BuildSceneCameraKey(const SceneDocument &scene_document) {
    if (!scene_document.GetScenePath().empty()) {
        return scene_document.GetScenePath().lexically_normal().generic_string();
    }
    return scene_document.GetSceneName();
}

void EnsureSceneCameraInitialized(const Engine &engine,
                                  const SceneDocument &scene_document) {
    const std::string scene_key = BuildSceneCameraKey(scene_document);
    if (g_scene_camera_state.initialized &&
        g_scene_camera_state.scene_key == scene_key) {
        return;
    }

    g_scene_camera_state.initialized = true;
    g_scene_camera_state.scene_key = scene_key;
    g_scene_camera_state.world_x = engine.GetCameraPositionX();
    g_scene_camera_state.world_y = engine.GetCameraPositionY();
    g_scene_camera_state.zoom = ClampSceneCameraZoom(engine.GetCameraZoom());
}

bool TryReadNumericProperty(const Actor::ComponentPropertyValue &value,
                            float &out_value) {
    if (const int *typed_value = std::get_if<int>(&value)) {
        out_value = static_cast<float>(*typed_value);
        return true;
    }
    if (const double *typed_value = std::get_if<double>(&value)) {
        out_value = static_cast<float>(*typed_value);
        return true;
    }
    return false;
}

bool TryReadBoolProperty(const Actor::ComponentPropertyValue &value,
                         bool &out_value) {
    if (const bool *typed_value = std::get_if<bool>(&value)) {
        out_value = *typed_value;
        return true;
    }
    return false;
}

bool TryReadStringProperty(const Actor::ComponentPropertyValue &value,
                           std::string &out_value) {
    if (const std::string *typed_value = std::get_if<std::string>(&value)) {
        out_value = *typed_value;
        return true;
    }
    return false;
}

bool ReadPropertyAsFloat(const std::vector<Actor::ComponentProperty> &properties,
                         const std::string &property_name,
                         float fallback_value, float &out_value) {
    for (const Actor::ComponentProperty &property : properties) {
        if (property.name != property_name) continue;
        if (!TryReadNumericProperty(property.value, out_value)) {
            out_value = fallback_value;
        }
        return true;
    }
    out_value = fallback_value;
    return false;
}

bool ReadPropertyAsBool(const std::vector<Actor::ComponentProperty> &properties,
                        const std::string &property_name, bool fallback_value,
                        bool &out_value) {
    for (const Actor::ComponentProperty &property : properties) {
        if (property.name != property_name) continue;
        if (!TryReadBoolProperty(property.value, out_value)) {
            out_value = fallback_value;
        }
        return true;
    }
    out_value = fallback_value;
    return false;
}

bool ReadPropertyAsString(
    const std::vector<Actor::ComponentProperty> &properties,
    const std::string &property_name, const std::string &fallback_value,
    std::string &out_value) {
    for (const Actor::ComponentProperty &property : properties) {
        if (property.name != property_name) continue;
        if (!TryReadStringProperty(property.value, out_value)) {
            out_value = fallback_value;
        }
        return true;
    }
    out_value = fallback_value;
    return false;
}

void RotateClockwise(float x, float y, float rotation_degrees, float &out_x,
                     float &out_y) {
    const float radians =
        rotation_degrees * (3.14159265358979323846f / 180.0f);
    const float cos_theta = std::cos(radians);
    const float sin_theta = std::sin(radians);
    out_x = cos_theta * x + sin_theta * y;
    out_y = -sin_theta * x + cos_theta * y;
}

ImVec2 WorldToScenePanelPosition(const Engine &engine,
                                 const SceneViewCameraState &scene_camera,
                                 const ImVec2 &image_min,
                                 const ImVec2 &image_size, float world_x,
                                 float world_y) {
    const float safe_zoom = std::max(0.0001f, scene_camera.zoom);
    const float runtime_width =
        static_cast<float>(std::max(1, engine.GetScenePreviewRenderTargetWidth()));
    const float runtime_height =
        static_cast<float>(std::max(1, engine.GetScenePreviewRenderTargetHeight()));
    const float runtime_pixel_x =
        (world_x - scene_camera.world_x) * kPixelsPerUnit * safe_zoom +
        runtime_width * 0.5f;
    const float runtime_pixel_y =
        (world_y - scene_camera.world_y) * kPixelsPerUnit * safe_zoom +
        runtime_height * 0.5f;

    return ImVec2(image_min.x + (runtime_pixel_x / runtime_width) * image_size.x,
                  image_min.y + (runtime_pixel_y / runtime_height) * image_size.y);
}

float WorldUnitsToScenePanelPixels(const Engine &engine,
                                   const SceneViewCameraState &scene_camera,
                                   const ImVec2 &image_size,
                                   float world_units) {
    const float safe_zoom = std::max(0.0001f, scene_camera.zoom);
    const float runtime_width =
        static_cast<float>(std::max(1, engine.GetScenePreviewRenderTargetWidth()));
    const float panel_pixels_per_runtime_pixel = image_size.x / runtime_width;
    return world_units * kPixelsPerUnit * safe_zoom *
           panel_pixels_per_runtime_pixel;
}

ImVec2 ScenePanelPixelsToWorldPosition(const Engine &engine,
                                       const SceneViewCameraState &scene_camera,
                                       const ImVec2 &image_min,
                                       const ImVec2 &image_size,
                                       const ImVec2 &panel_position) {
    const float safe_zoom = std::max(0.0001f, scene_camera.zoom);
    const float runtime_width =
        static_cast<float>(std::max(1, engine.GetScenePreviewRenderTargetWidth()));
    const float runtime_height =
        static_cast<float>(std::max(1, engine.GetScenePreviewRenderTargetHeight()));
    const float normalized_x =
        (panel_position.x - image_min.x) / std::max(1.0f, image_size.x);
    const float normalized_y =
        (panel_position.y - image_min.y) / std::max(1.0f, image_size.y);
    const float runtime_pixel_x = normalized_x * runtime_width;
    const float runtime_pixel_y = normalized_y * runtime_height;

    return ImVec2(
        ((runtime_pixel_x - runtime_width * 0.5f) /
         (kPixelsPerUnit * safe_zoom)) +
            scene_camera.world_x,
        ((runtime_pixel_y - runtime_height * 0.5f) /
         (kPixelsPerUnit * safe_zoom)) +
            scene_camera.world_y);
}

void ResetSceneDragState() {
    g_scene_drag_state = SceneDragState{};
}

std::optional<ActorDragTarget> BuildActorDragTarget(
    const std::vector<Actor::ComponentSpec> &component_specs,
    const std::function<std::vector<Actor::ComponentProperty>(
        const std::string &component_key)> &property_lookup,
    std::size_t actor_index, int runtime_actor_id,
    std::uint64_t editor_actor_uid) {
    ActorDragTarget target;
    target.actor_index = actor_index;
    target.runtime_actor_id = runtime_actor_id;
    target.editor_actor_uid = editor_actor_uid;

    // Scene editing should prefer the authored Transform component when one is
    // present, instead of whichever arbitrary x/y-bearing component happens to
    // appear first in key order.
    for (const Actor::ComponentSpec &component_spec : component_specs) {
        if (component_spec.type != "Transform") continue;
        const std::vector<Actor::ComponentProperty> properties =
            property_lookup(component_spec.key);
        if (!ReadPropertyAsFloat(properties, "x", 0.0f, target.world_x) ||
            !ReadPropertyAsFloat(properties, "y", 0.0f, target.world_y)) {
            continue;
        }
        target.component_key = component_spec.key;
        return target;
    }

    for (const Actor::ComponentSpec &component_spec : component_specs) {
        if (component_spec.type != "Rigidbody") continue;
        const std::vector<Actor::ComponentProperty> properties =
            property_lookup(component_spec.key);
        if (!ReadPropertyAsFloat(properties, "x", 0.0f, target.world_x) ||
            !ReadPropertyAsFloat(properties, "y", 0.0f, target.world_y)) {
            continue;
        }
        target.component_key = component_spec.key;
        target.transform_component = true;
        return target;
    }

    for (const Actor::ComponentSpec &component_spec : component_specs) {
        const std::vector<Actor::ComponentProperty> properties =
            property_lookup(component_spec.key);
        if (!ReadPropertyAsFloat(properties, "x", 0.0f, target.world_x) ||
            !ReadPropertyAsFloat(properties, "y", 0.0f, target.world_y)) {
            continue;
        }
        target.component_key = component_spec.key;
        return target;
    }

    return std::nullopt;
}

bool HasComponentType(const std::vector<Actor::ComponentSpec> &component_specs,
                      const std::string &component_type) {
    return std::any_of(component_specs.begin(), component_specs.end(),
                       [&](const Actor::ComponentSpec &component_spec) {
                           return component_spec.type == component_type;
                       });
}

ActorScreenBounds BuildTransformAnchorBounds(
                                             const Engine &engine,
                                             const SceneViewCameraState &scene_camera,
                                             std::size_t actor_index,
                                             int runtime_actor_id,
                                             std::uint64_t editor_actor_uid,
                                             float world_x, float world_y,
                                             const ImVec2 &image_min,
                                             const ImVec2 &image_size) {
    ActorScreenBounds bounds;
    bounds.actor_index = actor_index;
    bounds.runtime_actor_id = runtime_actor_id;
    bounds.editor_actor_uid = editor_actor_uid;
    bounds.circular = true;

    const ImVec2 panel_center = WorldToScenePanelPosition(
        engine, scene_camera, image_min, image_size, world_x, world_y);

    bounds.center_x = panel_center.x;
    bounds.center_y = panel_center.y;
    bounds.half_width = WorldUnitsToScenePanelPixels(
        engine, scene_camera, image_size, kFallbackSelectionHalfExtentUnits);
    bounds.half_height = bounds.half_width;
    return bounds;
}

std::optional<ActorScreenBounds> BuildActorScreenBounds(
    const Engine &engine, const SceneViewCameraState &scene_camera,
    const std::vector<Actor::ComponentSpec> &component_specs,
    const std::function<std::vector<Actor::ComponentProperty>(
        const std::string &component_key)> &property_lookup,
    std::size_t actor_index, int runtime_actor_id,
    std::uint64_t editor_actor_uid, const ImVec2 &image_min,
    const ImVec2 &image_size) {
    ActorScreenBounds bounds;
    bounds.actor_index = actor_index;
    bounds.runtime_actor_id = runtime_actor_id;
    bounds.editor_actor_uid = editor_actor_uid;

    // v1 picking uses Rigidbody-derived bounds because they already carry a
    // stable position and shape in engine world units.
    for (const Actor::ComponentSpec &component_spec : component_specs) {
        if (component_spec.type != "Rigidbody") continue;

        const std::vector<Actor::ComponentProperty> properties =
            property_lookup(component_spec.key);

        float x = 0.0f;
        float y = 0.0f;
        ReadPropertyAsFloat(properties, "x", 0.0f, x);
        ReadPropertyAsFloat(properties, "y", 0.0f, y);

        bool has_collider = true;
        bool has_trigger = true;
        ReadPropertyAsBool(properties, "has_collider", true, has_collider);
        ReadPropertyAsBool(properties, "has_trigger", true, has_trigger);

        std::string shape_type;
        if (has_collider) {
            ReadPropertyAsString(properties, "collider_type", "box", shape_type);
        } else if (has_trigger) {
            ReadPropertyAsString(properties, "trigger_type", "box", shape_type);
        } else {
            shape_type = "box";
        }

        float width = 1.0f;
        float height = 1.0f;
        float radius = 0.5f;
        if (has_collider) {
            ReadPropertyAsFloat(properties, "width", 1.0f, width);
            ReadPropertyAsFloat(properties, "height", 1.0f, height);
            ReadPropertyAsFloat(properties, "radius", 0.5f, radius);
        } else if (has_trigger) {
            ReadPropertyAsFloat(properties, "trigger_width", 1.0f, width);
            ReadPropertyAsFloat(properties, "trigger_height", 1.0f, height);
            ReadPropertyAsFloat(properties, "trigger_radius", 0.5f, radius);
        }

        bounds.circular = (shape_type == "circle");
        const ImVec2 panel_center = WorldToScenePanelPosition(
            engine, scene_camera, image_min, image_size, x, y);
        bounds.center_x = panel_center.x;
        bounds.center_y = panel_center.y;

        if (bounds.circular) {
            const float pixel_radius = WorldUnitsToScenePanelPixels(
                engine, scene_camera, image_size, radius);
            bounds.half_width = pixel_radius;
            bounds.half_height = pixel_radius;
        } else {
            bounds.half_width = WorldUnitsToScenePanelPixels(
                engine, scene_camera, image_size, width * 0.5f);
            bounds.half_height = WorldUnitsToScenePanelPixels(
                engine, scene_camera, image_size, height * 0.5f);
        }
        return bounds;
    }

    for (const Actor::ComponentSpec &component_spec : component_specs) {
        if (component_spec.type != "Transform") continue;

        const std::vector<Actor::ComponentProperty> properties =
            property_lookup(component_spec.key);
        float x = 0.0f;
        float y = 0.0f;
        const bool has_x = ReadPropertyAsFloat(properties, "x", 0.0f, x);
        const bool has_y = ReadPropertyAsFloat(properties, "y", 0.0f, y);
        if (!has_x || !has_y) continue;

        const ImVec2 panel_center = WorldToScenePanelPosition(
            engine, scene_camera, image_min, image_size, x, y);

        bounds.circular = true;
        bounds.center_x = panel_center.x;
        bounds.center_y = panel_center.y;
        bounds.half_width = WorldUnitsToScenePanelPixels(
            engine, scene_camera, image_size, kFallbackSelectionHalfExtentUnits);
        bounds.half_height = bounds.half_width;
        return bounds;
    }

    // Fallback for non-physics actors: if any component exposes scalar x/y
    // properties, offer a small clickable handle around that anchor point.
    for (const Actor::ComponentSpec &component_spec : component_specs) {
        const std::vector<Actor::ComponentProperty> properties =
            property_lookup(component_spec.key);
        float x = 0.0f;
        float y = 0.0f;
        const bool has_x = ReadPropertyAsFloat(properties, "x", 0.0f, x);
        const bool has_y = ReadPropertyAsFloat(properties, "y", 0.0f, y);
        if (!has_x || !has_y) continue;

        const ImVec2 panel_center = WorldToScenePanelPosition(
            engine, scene_camera, image_min, image_size, x, y);

        bounds.circular = true;
        bounds.center_x = panel_center.x;
        bounds.center_y = panel_center.y;
        bounds.half_width = WorldUnitsToScenePanelPixels(
            engine, scene_camera, image_size, kFallbackSelectionHalfExtentUnits);
        bounds.half_height = bounds.half_width;
        return bounds;
    }

    return std::nullopt;
}

std::optional<ActorScreenBounds> BuildSceneActorScreenBounds(
    const Engine &engine, const SceneViewCameraState &scene_camera,
    SceneDocument &scene_document, std::size_t actor_index,
    const ImVec2 &image_min, const ImVec2 &image_size) {
    const Actor effective_actor = scene_document.BuildEffectiveActor(actor_index);
    if (!HasComponentType(effective_actor.component_specs, "Rigidbody")) {
        float world_x = 0.0f;
        float world_y = 0.0f;
        float world_rotation = 0.0f;
        if (scene_document.TryGetActorWorldTransform(actor_index, nullptr,
                                                     world_x, world_y,
                                                     world_rotation)) {
            return BuildTransformAnchorBounds(
                engine, scene_camera, actor_index, -1,
                scene_document.GetActorUID(actor_index),
                world_x, world_y, image_min, image_size);
        }
    }
    return BuildActorScreenBounds(
        engine, scene_camera, effective_actor.component_specs,
        [&scene_document, actor_index](const std::string &component_key) {
            return scene_document.GetInspectableProperties(actor_index,
                                                          component_key);
        },
        actor_index, -1, scene_document.GetActorUID(actor_index), image_min,
        image_size);
}

std::optional<ActorDragTarget> BuildSceneActorDragTarget(
    SceneDocument &scene_document, std::size_t actor_index) {
    const Actor effective_actor = scene_document.BuildEffectiveActor(actor_index);
    if (!HasComponentType(effective_actor.component_specs, "Rigidbody")) {
        float world_x = 0.0f;
        float world_y = 0.0f;
        float world_rotation = 0.0f;
        std::string transform_component_key;
        if (scene_document.TryGetActorWorldTransform(actor_index,
                                                     &transform_component_key,
                                                     world_x, world_y,
                                                     world_rotation)) {
            ActorDragTarget target;
            target.actor_index = actor_index;
            target.editor_actor_uid = scene_document.GetActorUID(actor_index);
            target.component_key = transform_component_key;
            target.world_x = world_x;
            target.world_y = world_y;
            target.transform_component = true;
            return target;
        }
    }
    return BuildActorDragTarget(
        effective_actor.component_specs,
        [&scene_document, actor_index](const std::string &component_key) {
            return scene_document.GetInspectableProperties(actor_index,
                                                          component_key);
        },
        actor_index, -1, scene_document.GetActorUID(actor_index));
}

std::optional<ActorScreenBounds> BuildRuntimeActorScreenBounds(
    const Engine &engine, const SceneViewCameraState &scene_camera,
    const Actor &runtime_actor, const ImVec2 &image_min,
    const ImVec2 &image_size) {
    const std::vector<Actor::ComponentSpec> runtime_components =
        ComponentManager::GetRuntimeComponentSpecs(runtime_actor.id);
    if (!HasComponentType(runtime_components, "Rigidbody")) {
        float world_x = 0.0f;
        float world_y = 0.0f;
        float world_rotation = 0.0f;
        if (ComponentManager::TryGetRuntimeTransformWorld(runtime_actor.id,
                                                          world_x, world_y,
                                                          world_rotation,
                                                          nullptr)) {
            return BuildTransformAnchorBounds(
                engine, scene_camera, std::numeric_limits<std::size_t>::max(),
                runtime_actor.id, runtime_actor.editor_actor_uid, world_x,
                world_y, image_min, image_size);
        }
    }
    return BuildActorScreenBounds(
        engine, scene_camera, runtime_components,
        [&runtime_actor](const std::string &component_key) {
            return ComponentManager::GetRuntimeComponentProperties(
                runtime_actor.id, component_key);
        },
        std::numeric_limits<std::size_t>::max(), runtime_actor.id,
        runtime_actor.editor_actor_uid, image_min, image_size);
}

std::optional<ActorDragTarget> BuildRuntimeActorDragTarget(
    SceneDocument &scene_document, const Actor &runtime_actor) {
    const std::vector<Actor::ComponentSpec> runtime_components =
        ComponentManager::GetRuntimeComponentSpecs(runtime_actor.id);
    std::size_t actor_index = std::numeric_limits<std::size_t>::max();
    if (runtime_actor.editor_actor_uid != Actor::kInvalidEditorActorUID) {
        const std::optional<std::size_t> scene_actor_index =
            scene_document.FindActorIndexByUID(runtime_actor.editor_actor_uid);
        if (scene_actor_index.has_value()) {
            actor_index = *scene_actor_index;
        }
    }
    if (!HasComponentType(runtime_components, "Rigidbody")) {
        float world_x = 0.0f;
        float world_y = 0.0f;
        float world_rotation = 0.0f;
        std::string transform_component_key;
        if (ComponentManager::TryGetRuntimeTransformWorld(
                runtime_actor.id, world_x, world_y, world_rotation,
                &transform_component_key)) {
            ActorDragTarget target;
            target.actor_index = actor_index;
            target.runtime_actor_id = runtime_actor.id;
            target.editor_actor_uid = runtime_actor.editor_actor_uid;
            target.component_key = transform_component_key;
            target.world_x = world_x;
            target.world_y = world_y;
            target.transform_component = true;
            return target;
        }
    }
    return BuildActorDragTarget(
        runtime_components,
        [&runtime_actor](const std::string &component_key) {
            return ComponentManager::GetRuntimeComponentProperties(
                runtime_actor.id, component_key);
        },
        actor_index, runtime_actor.id, runtime_actor.editor_actor_uid);
}

std::vector<ActorScreenBounds> BuildPickBoundsForClick(
    const Engine &engine, const SceneViewCameraState &scene_camera,
    SceneDocument &scene_document, bool play_mode_active,
    const ImVec2 &image_min, const ImVec2 &image_size) {
    std::vector<ActorScreenBounds> pick_bounds;

    // Full pick lists are only needed on the exact click frame. Building them
    // every frame is expensive because runtime lookups walk live Lua-backed
    // component state for every actor.
    if (play_mode_active) {
        const std::deque<Actor> &runtime_actors = engine.GetRuntimeActors();
        pick_bounds.reserve(runtime_actors.size());
        for (const Actor &runtime_actor : runtime_actors) {
            if (runtime_actor.runtime_destroyed) continue;
            const std::optional<ActorScreenBounds> bounds =
                BuildRuntimeActorScreenBounds(engine, scene_camera,
                                              runtime_actor, image_min,
                                              image_size);
            if (!bounds.has_value()) continue;
            pick_bounds.emplace_back(*bounds);
        }
        return pick_bounds;
    }

    pick_bounds.reserve(scene_document.GetActorCount());
    for (std::size_t actor_index = 0; actor_index < scene_document.GetActorCount();
         ++actor_index) {
        const std::optional<ActorScreenBounds> bounds =
            BuildSceneActorScreenBounds(engine, scene_camera, scene_document,
                                       actor_index,
                                       image_min, image_size);
        if (!bounds.has_value()) continue;
        pick_bounds.emplace_back(*bounds);
    }
    return pick_bounds;
}

std::optional<ActorDragTarget> BuildSelectedActorDragTarget(
    const Engine &engine, SceneDocument &scene_document,
    int selected_actor_index, int selected_runtime_actor_id,
    bool play_mode_active) {
    if (play_mode_active) {
        if (selected_runtime_actor_id < 0) return std::nullopt;
        const Actor *runtime_actor =
            engine.GetRuntimeActorByID(selected_runtime_actor_id);
        if (runtime_actor == nullptr || runtime_actor->runtime_destroyed) {
            return std::nullopt;
        }
        return BuildRuntimeActorDragTarget(scene_document, *runtime_actor);
    }

    if (selected_actor_index < 0 ||
        selected_actor_index >= static_cast<int>(scene_document.GetActorCount())) {
        return std::nullopt;
    }
    return BuildSceneActorDragTarget(
        scene_document, static_cast<std::size_t>(selected_actor_index));
}

std::optional<ActorScreenBounds> BuildSelectedActorBounds(
    const Engine &engine, const SceneViewCameraState &scene_camera,
    SceneDocument &scene_document,
    int selected_actor_index, int selected_runtime_actor_id,
    bool play_mode_active, const ImVec2 &image_min, const ImVec2 &image_size) {
    if (play_mode_active) {
        if (selected_runtime_actor_id < 0) return std::nullopt;
        const Actor *runtime_actor =
            engine.GetRuntimeActorByID(selected_runtime_actor_id);
        if (runtime_actor == nullptr || runtime_actor->runtime_destroyed) {
            return std::nullopt;
        }
        return BuildRuntimeActorScreenBounds(engine, scene_camera,
                                             *runtime_actor, image_min,
                                             image_size);
    }

    if (selected_actor_index < 0 ||
        selected_actor_index >= static_cast<int>(scene_document.GetActorCount())) {
        return std::nullopt;
    }
    return BuildSceneActorScreenBounds(
        engine, scene_camera, scene_document,
        static_cast<std::size_t>(selected_actor_index),
        image_min, image_size);
}

bool IsPointInsideActorBounds(const ActorScreenBounds &bounds,
                              const ImVec2 &point) {
    const float dx = point.x - bounds.center_x;
    const float dy = point.y - bounds.center_y;
    if (bounds.circular) {
        const float radius = std::max(bounds.half_width, bounds.half_height);
        return dx * dx + dy * dy <= radius * radius;
    }
    return std::abs(dx) <= bounds.half_width && std::abs(dy) <= bounds.half_height;
}

std::optional<ActorScreenBounds> FindTopmostHitBounds(
    const ImVec2 &mouse_position,
    const std::vector<ActorScreenBounds> &pick_bounds) {
    for (auto it = pick_bounds.rbegin(); it != pick_bounds.rend(); ++it) {
        if (IsPointInsideActorBounds(*it, mouse_position)) {
            return *it;
        }
    }
    return std::nullopt;
}

void DrawActorSelectionOutline(ImDrawList *draw_list,
                               const ActorScreenBounds &bounds) {
    const ImU32 glow_color = IM_COL32(90, 168, 255, 120);
    const ImU32 border_color = IM_COL32(120, 200, 255, 255);

    if (bounds.circular) {
        const float radius =
            std::max(bounds.half_width, bounds.half_height) +
            kSelectionPaddingPixels;
        draw_list->AddCircle(ImVec2(bounds.center_x, bounds.center_y), radius + 2.0f,
                             glow_color, 0, 5.0f);
        draw_list->AddCircle(ImVec2(bounds.center_x, bounds.center_y), radius,
                             border_color, 0, kSelectionStrokeThickness);
        return;
    }

    const ImVec2 min(bounds.center_x - bounds.half_width - kSelectionPaddingPixels,
                     bounds.center_y - bounds.half_height - kSelectionPaddingPixels);
    const ImVec2 max(bounds.center_x + bounds.half_width + kSelectionPaddingPixels,
                     bounds.center_y + bounds.half_height + kSelectionPaddingPixels);
    draw_list->AddRect(min, max, glow_color, 8.0f, 0, 5.0f);
    draw_list->AddRect(min, max, border_color, 8.0f, 0,
                       kSelectionStrokeThickness);
}

void HandleSceneSelectionClick(const ImVec2 &mouse_position,
                               const std::vector<ActorScreenBounds> &pick_bounds,
                               SceneDocument &scene_document,
                               bool play_mode_active,
                               int &selected_actor_index,
                               int &selected_runtime_actor_id) {
    for (auto it = pick_bounds.rbegin(); it != pick_bounds.rend(); ++it) {
        if (!IsPointInsideActorBounds(*it, mouse_position)) continue;
        if (play_mode_active) {
            selected_runtime_actor_id = it->runtime_actor_id;
            if (it->editor_actor_uid != Actor::kInvalidEditorActorUID) {
                const std::optional<std::size_t> actor_index =
                    scene_document.FindActorIndexByUID(it->editor_actor_uid);
                selected_actor_index =
                    actor_index.has_value() ? static_cast<int>(*actor_index)
                                            : -1;
            } else {
                selected_actor_index = -1;
            }
        } else {
            selected_actor_index = static_cast<int>(it->actor_index);
            selected_runtime_actor_id = -1;
        }
        return;
    }
    selected_actor_index = -1;
    selected_runtime_actor_id = -1;
}

bool HandleSceneTemplateDrop(
    const Engine &engine, const SceneViewCameraState &scene_camera,
    SceneDocument &scene_document, const ImVec2 &image_min,
    const ImVec2 &image_size, int &selected_actor_index,
    int &selected_runtime_actor_id,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    if (!ImGui::BeginDragDropTarget()) return false;

    bool scene_changed = false;
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload(EditorDragDrop::kActorTemplatePayload)) {
        const char *template_name = static_cast<const char *>(payload->Data);
        if (template_name != nullptr && template_name[0] != '\0') {
            const ImVec2 drop_world_position = ScenePanelPixelsToWorldPosition(
                engine, scene_camera, image_min, image_size,
                ImGui::GetMousePos());

            std::size_t new_actor_index = 0;
            SceneFormat::SceneEditCommand create_command;
            if (scene_document.AppendActorFromTemplate(template_name,
                                                      new_actor_index,
                                                      &create_command)) {
                scene_changed = true;
                if (out_edit_commands != nullptr) {
                    out_edit_commands->emplace_back(std::move(create_command));
                }

                std::string transform_component_key;
                float local_x = 0.0f;
                float local_y = 0.0f;
                float local_rotation = 0.0f;
                if (scene_document.TryGetActorLocalTransform(
                        new_actor_index, &transform_component_key, local_x,
                        local_y, local_rotation)) {
                    SceneFormat::SceneEditCommand x_command;
                    if (scene_document.SetComponentProperty(
                            new_actor_index, transform_component_key, "x",
                            static_cast<double>(drop_world_position.x),
                            &x_command)) {
                        if (out_edit_commands != nullptr) {
                            out_edit_commands->emplace_back(
                                std::move(x_command));
                        }
                    }

                    SceneFormat::SceneEditCommand y_command;
                    if (scene_document.SetComponentProperty(
                            new_actor_index, transform_component_key, "y",
                            static_cast<double>(drop_world_position.y),
                            &y_command)) {
                        if (out_edit_commands != nullptr) {
                            out_edit_commands->emplace_back(
                                std::move(y_command));
                        }
                    }
                }

                selected_actor_index = static_cast<int>(new_actor_index);
                selected_runtime_actor_id = -1;
            }
        }
    }

    ImGui::EndDragDropTarget();
    return scene_changed;
}

bool AppendPositionCommands(SceneDocument &scene_document,
                            std::size_t actor_index,
                            const std::string &component_key, float world_x,
                            float world_y,
                            std::vector<SceneFormat::SceneEditCommand>
                                *out_edit_commands) {
    bool changed = false;

    SceneFormat::SceneEditCommand command;
    if (scene_document.SetComponentProperty(
            actor_index, component_key, "x", static_cast<double>(world_x),
            &command)) {
        changed = true;
        if (out_edit_commands != nullptr) {
            out_edit_commands->emplace_back(std::move(command));
        }
    }

    if (scene_document.SetComponentProperty(
            actor_index, component_key, "y", static_cast<double>(world_y),
            &command)) {
        changed = true;
        if (out_edit_commands != nullptr) {
            out_edit_commands->emplace_back(std::move(command));
        }
    }

    return changed;
}

void ConvertSceneWorldPositionToLocal(SceneDocument &scene_document,
                                      std::size_t actor_index, float world_x,
                                      float world_y, float &out_local_x,
                                      float &out_local_y) {
    const std::optional<std::size_t> parent_actor_index =
        scene_document.FindParentActorIndex(actor_index);
    if (!parent_actor_index.has_value()) {
        out_local_x = world_x;
        out_local_y = world_y;
        return;
    }

    float parent_world_x = 0.0f;
    float parent_world_y = 0.0f;
    float parent_world_rotation = 0.0f;
    if (!scene_document.TryGetActorWorldTransform(*parent_actor_index, nullptr,
                                                  parent_world_x,
                                                  parent_world_y,
                                                  parent_world_rotation)) {
        out_local_x = world_x;
        out_local_y = world_y;
        return;
    }

    const float relative_world_x = world_x - parent_world_x;
    const float relative_world_y = world_y - parent_world_y;
    RotateClockwise(relative_world_x, relative_world_y, -parent_world_rotation,
                    out_local_x, out_local_y);
}

bool ApplyDraggedActorPosition(
    SceneDocument &scene_document, const ActorDragTarget &drag_target,
    bool play_mode_active, float world_x, float world_y,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    if (drag_target.transform_component && play_mode_active &&
        drag_target.actor_index == std::numeric_limits<std::size_t>::max()) {
        return ComponentManager::SetRuntimeTransformWorldPosition(
            drag_target.runtime_actor_id, world_x, world_y);
    }

    if (drag_target.transform_component) {
        if (drag_target.actor_index == std::numeric_limits<std::size_t>::max()) {
            return false;
        }

        float local_x = world_x;
        float local_y = world_y;
        ConvertSceneWorldPositionToLocal(scene_document, drag_target.actor_index,
                                         world_x, world_y, local_x, local_y);
        return AppendPositionCommands(scene_document, drag_target.actor_index,
                                      drag_target.component_key, local_x,
                                      local_y, out_edit_commands);
    }

    if (play_mode_active &&
        drag_target.actor_index == std::numeric_limits<std::size_t>::max()) {
        bool changed = false;
        changed |= ComponentManager::SetRuntimeComponentPropertyValue(
            drag_target.runtime_actor_id, drag_target.component_key, "x",
            static_cast<double>(world_x));
        changed |= ComponentManager::SetRuntimeComponentPropertyValue(
            drag_target.runtime_actor_id, drag_target.component_key, "y",
            static_cast<double>(world_y));
        return changed;
    }

    if (drag_target.actor_index == std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    return AppendPositionCommands(scene_document, drag_target.actor_index,
                                  drag_target.component_key, world_x, world_y,
                                  out_edit_commands);
}

void BeginSelectedActorDrag(const Engine &engine,
                            const SceneViewCameraState &scene_camera,
                            SceneDocument &scene_document,
                            const ImVec2 &image_min, const ImVec2 &image_size,
                            int selected_actor_index,
                            int selected_runtime_actor_id,
                            bool play_mode_active) {
    const std::optional<ActorDragTarget> drag_target =
        BuildSelectedActorDragTarget(engine, scene_document,
                                     selected_actor_index,
                                     selected_runtime_actor_id,
                                     play_mode_active);
    if (!drag_target.has_value()) {
        ResetSceneDragState();
        return;
    }

    const ImVec2 mouse_world_position = ScenePanelPixelsToWorldPosition(
        engine, scene_camera, image_min, image_size, ImGui::GetMousePos());
    g_scene_drag_state.active = true;
    g_scene_drag_state.play_mode_active = play_mode_active;
    g_scene_drag_state.actor_index = drag_target->actor_index;
    g_scene_drag_state.runtime_actor_id = drag_target->runtime_actor_id;
    g_scene_drag_state.editor_actor_uid = drag_target->editor_actor_uid;
    g_scene_drag_state.component_key = drag_target->component_key;
    g_scene_drag_state.world_offset_x =
        drag_target->world_x - mouse_world_position.x;
    g_scene_drag_state.world_offset_y =
        drag_target->world_y - mouse_world_position.y;
}

bool UpdateSelectedActorDrag(
    const Engine &engine, const SceneViewCameraState &scene_camera,
    SceneDocument &scene_document,
    const ImVec2 &image_min, const ImVec2 &image_size,
    int selected_actor_index, int selected_runtime_actor_id,
    bool play_mode_active,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    if (!g_scene_drag_state.active) return false;
    if (g_scene_drag_state.play_mode_active != play_mode_active) {
        ResetSceneDragState();
        return false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ResetSceneDragState();
        return false;
    }
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        return false;
    }

    const std::optional<ActorDragTarget> drag_target =
        BuildSelectedActorDragTarget(engine, scene_document,
                                     selected_actor_index,
                                     selected_runtime_actor_id,
                                     play_mode_active);
    if (!drag_target.has_value() ||
        drag_target->component_key != g_scene_drag_state.component_key ||
        drag_target->runtime_actor_id != g_scene_drag_state.runtime_actor_id ||
        drag_target->editor_actor_uid != g_scene_drag_state.editor_actor_uid) {
        ResetSceneDragState();
        return false;
    }

    const ImVec2 mouse_world_position = ScenePanelPixelsToWorldPosition(
        engine, scene_camera, image_min, image_size, ImGui::GetMousePos());
    const float target_world_x =
        mouse_world_position.x + g_scene_drag_state.world_offset_x;
    const float target_world_y =
        mouse_world_position.y + g_scene_drag_state.world_offset_y;
    return ApplyDraggedActorPosition(scene_document, *drag_target,
                                     play_mode_active, target_world_x,
                                     target_world_y, out_edit_commands);
}

void PanSceneCamera(const Engine &engine, const ImVec2 &image_size,
                    const ImVec2 &mouse_delta) {
    const float runtime_width =
        static_cast<float>(std::max(1, engine.GetScenePreviewRenderTargetWidth()));
    const float runtime_height =
        static_cast<float>(std::max(1, engine.GetScenePreviewRenderTargetHeight()));
    const float safe_zoom = std::max(0.0001f, g_scene_camera_state.zoom);
    const float world_delta_x =
        mouse_delta.x * runtime_width /
        std::max(1.0f, image_size.x * kPixelsPerUnit * safe_zoom);
    const float world_delta_y =
        mouse_delta.y * runtime_height /
        std::max(1.0f, image_size.y * kPixelsPerUnit * safe_zoom);

    g_scene_camera_state.world_x -= world_delta_x;
    g_scene_camera_state.world_y -= world_delta_y;
}

void ZoomSceneCameraAtCursor(const Engine &engine, const ImVec2 &image_min,
                             const ImVec2 &image_size,
                             const ImVec2 &mouse_position,
                             float wheel_delta) {
    if (std::abs(wheel_delta) <= 0.0001f) return;

    const ImVec2 world_before_zoom = ScenePanelPixelsToWorldPosition(
        engine, g_scene_camera_state, image_min, image_size, mouse_position);
    const float zoom_scale = std::pow(1.15f, wheel_delta);
    g_scene_camera_state.zoom =
        ClampSceneCameraZoom(g_scene_camera_state.zoom * zoom_scale);
    const ImVec2 world_after_zoom = ScenePanelPixelsToWorldPosition(
        engine, g_scene_camera_state, image_min, image_size, mouse_position);
    g_scene_camera_state.world_x += world_before_zoom.x - world_after_zoom.x;
    g_scene_camera_state.world_y += world_before_zoom.y - world_after_zoom.y;
}

} // namespace

ScenePanelResult RenderScenePanel(
    Engine &engine, SceneDocument &scene_document,
    int &selected_actor_index, int &selected_runtime_actor_id,
    int scene_view_width, int scene_view_height, bool play_mode_active,
    bool play_mode_paused, bool scene_editing_enabled,
    std::vector<SceneFormat::SceneEditCommand> *out_edit_commands) {
    ScenePanelResult controls_result;
    EnsureSceneCameraInitialized(engine, scene_document);
    const std::size_t scene_panel_command_begin_index =
        (out_edit_commands != nullptr) ? out_edit_commands->size() : 0;
    bool scene_panel_commands_applied_immediately = false;

    // Scene view is now the interactive home for the embedded runtime image.
    // Selection and gizmos will be layered on top of this same surface next.
    ImGui::Begin("Scene", nullptr,
                 ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    SDL_Texture *scene_preview_texture =
        engine.RenderScenePreview(g_scene_camera_state.world_x,
                                  g_scene_camera_state.world_y,
                                  g_scene_camera_state.zoom,
                                  scene_view_width, scene_view_height,
                                  !play_mode_active);
    if (scene_preview_texture == nullptr) {
        ImGui::TextUnformatted("Runtime scene view is not available yet.");
        ImGui::End();
        return controls_result;
    }

    const ImVec2 available_size = ImGui::GetContentRegionAvail();
    const ImVec2 image_size = FitPreviewImage(
        available_size, engine.GetScenePreviewRenderTargetWidth(),
        engine.GetScenePreviewRenderTargetHeight());
    const ImVec2 cursor_screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 centered_cursor(
        cursor_screen_pos.x +
            std::max(0.0f, (available_size.x - image_size.x) * 0.5f),
        cursor_screen_pos.y +
            std::max(0.0f, (available_size.y - image_size.y) * 0.5f));
    const ImVec2 image_max(centered_cursor.x + image_size.x,
                           centered_cursor.y + image_size.y);

    ImGui::SetCursorScreenPos(centered_cursor);
    ImGui::InvisibleButton("scene_canvas", image_size,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool image_hovered = ImGui::IsItemHovered();
    const bool image_active = ImGui::IsItemActive();
    const bool scene_canvas_engaged = image_hovered || image_active;
    bool preview_refresh_requested = false;

    controls_result.has_runtime_image = true;
    controls_result.runtime_image_min_x = centered_cursor.x;
    controls_result.runtime_image_min_y = centered_cursor.y;
    controls_result.runtime_image_max_x = image_max.x;
    controls_result.runtime_image_max_y = image_max.y;

    if (image_hovered && std::abs(ImGui::GetIO().MouseWheel) > 0.0001f) {
        ZoomSceneCameraAtCursor(engine, centered_cursor, image_size,
                                ImGui::GetMousePos(),
                                ImGui::GetIO().MouseWheel);
        preview_refresh_requested = true;
    }

    if (scene_canvas_engaged &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
        PanSceneCamera(engine, image_size, ImGui::GetIO().MouseDelta);
        ResetSceneDragState();
        preview_refresh_requested = true;
    }

    if (scene_editing_enabled && image_hovered) {
        const std::size_t command_count_before_drop =
            (out_edit_commands != nullptr) ? out_edit_commands->size() : 0;
        const bool dropped_template = HandleSceneTemplateDrop(
            engine, g_scene_camera_state, scene_document, centered_cursor,
            image_size, selected_actor_index, selected_runtime_actor_id,
            out_edit_commands);
        controls_result.scene_changed |= dropped_template;
        if (dropped_template) {
            preview_refresh_requested = true;
            if (out_edit_commands != nullptr) {
                for (std::size_t command_index = command_count_before_drop;
                     command_index < out_edit_commands->size();
                     ++command_index) {
                    engine.ApplySceneEditCommand(
                        (*out_edit_commands)[command_index]);
                }
                scene_panel_commands_applied_immediately = true;
            }
        }
    }

    if (scene_editing_enabled && image_hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const std::vector<ActorScreenBounds> pick_bounds =
            BuildPickBoundsForClick(engine, g_scene_camera_state,
                                    scene_document, play_mode_active,
                                    centered_cursor, image_size);
        const ImVec2 mouse_position = ImGui::GetMousePos();
        const std::optional<ActorScreenBounds> hit_bounds =
            FindTopmostHitBounds(mouse_position, pick_bounds);
        HandleSceneSelectionClick(mouse_position, pick_bounds, scene_document,
                                  play_mode_active, selected_actor_index,
                                  selected_runtime_actor_id);
        if (hit_bounds.has_value()) {
            BeginSelectedActorDrag(engine, g_scene_camera_state,
                                   scene_document, centered_cursor, image_size,
                                   selected_actor_index,
                                   selected_runtime_actor_id,
                                   play_mode_active);
        } else {
            ResetSceneDragState();
        }
    }

    const std::size_t command_count_before_drag =
        (out_edit_commands != nullptr) ? out_edit_commands->size() : 0;
    controls_result.scene_changed |= UpdateSelectedActorDrag(
        engine, g_scene_camera_state, scene_document, centered_cursor,
        image_size,
        selected_actor_index, selected_runtime_actor_id, play_mode_active,
        out_edit_commands);
    if (controls_result.scene_changed) {
        preview_refresh_requested = true;
        if (out_edit_commands != nullptr) {
            for (std::size_t command_index = command_count_before_drag;
                 command_index < out_edit_commands->size(); ++command_index) {
                engine.ApplySceneEditCommand((*out_edit_commands)[command_index]);
            }
            scene_panel_commands_applied_immediately = true;
        }
    }

    if (preview_refresh_requested) {
        scene_preview_texture = engine.RenderScenePreview(
            g_scene_camera_state.world_x, g_scene_camera_state.world_y,
            g_scene_camera_state.zoom, scene_view_width, scene_view_height,
            !play_mode_active);
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(centered_cursor, image_max,
                             IM_COL32(36, 40, 46, 255), 4.0f);
    if (scene_preview_texture != nullptr) {
        draw_list->AddImage(
            ImTextureRef((ImTextureID)(intptr_t)scene_preview_texture),
            centered_cursor, image_max);
    }

    draw_list->AddRect(centered_cursor, image_max, IM_COL32(90, 168, 255, 255),
                       4.0f, 0, 2.0f);
    draw_list->AddText(ImVec2(centered_cursor.x + 10.0f,
                              centered_cursor.y + 10.0f),
                       IM_COL32(215, 220, 230, 210), "SCENE");
    draw_list->AddText(
        ImVec2(centered_cursor.x + 10.0f, centered_cursor.y + 30.0f),
        IM_COL32(175, 185, 198, 215),
        "Wheel: zoom  |  Right drag: pan");
    if (play_mode_active && !scene_editing_enabled) {
        draw_list->AddText(ImVec2(centered_cursor.x + 10.0f,
                                  centered_cursor.y + 50.0f),
                           IM_COL32(245, 205, 120, 235),
                           play_mode_paused ? "READ-ONLY (PAUSED)"
                                            : "READ-ONLY DURING PLAY");
    }

    const std::optional<ActorScreenBounds> selected_bounds =
        BuildSelectedActorBounds(engine, g_scene_camera_state, scene_document,
                                 selected_actor_index,
                                 selected_runtime_actor_id, play_mode_active,
                                 centered_cursor, image_size);
    if (selected_bounds.has_value()) {
        DrawActorSelectionOutline(draw_list, *selected_bounds);
    }

    ImGui::SetCursorScreenPos(
        ImVec2(cursor_screen_pos.x, centered_cursor.y + image_size.y));
    ImGui::Dummy(ImVec2(
        available_size.x,
        std::max(0.0f, available_size.y - image_size.y)));
    if (scene_panel_commands_applied_immediately && out_edit_commands != nullptr) {
        controls_result.immediate_apply_begin_index =
            scene_panel_command_begin_index;
        controls_result.immediate_apply_end_index = out_edit_commands->size();
    }
    ImGui::End();
    return controls_result;
}

} // namespace EditorPanels
