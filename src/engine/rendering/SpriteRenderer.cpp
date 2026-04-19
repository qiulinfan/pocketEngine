#include "rendering/SpriteRenderer.h"
#include "physics/Rigidbody.h"
#include "rendering/Renderer.h"
#include "scene/Actor.h"
#include "scene/Transform.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"

namespace {

bool TryReadTransformState(const Actor *actor, float &out_x, float &out_y,
                           float &out_rotation) {
    if (actor == nullptr) return false;

    const luabridge::LuaRef transform_ref = actor->GetComponent("Transform");
    if (transform_ref.isNil()) return false;

    Transform *transform = nullptr;
    try {
        transform = transform_ref.cast<Transform *>();
    } catch (const luabridge::LuaException &) {
        return false;
    }
    if (transform == nullptr) return false;

    out_x = transform->world_x;
    out_y = transform->world_y;
    out_rotation = transform->world_rotation;
    return true;
}

bool TryReadRigidbodyState(const Actor *actor, float &out_x, float &out_y,
                           float &out_rotation) {
    if (actor == nullptr) return false;

    const luabridge::LuaRef rigidbody_ref = actor->GetComponent("Rigidbody");
    if (rigidbody_ref.isNil()) return false;

    Rigidbody *rigidbody = nullptr;
    try {
        rigidbody = rigidbody_ref.cast<Rigidbody *>();
    } catch (const luabridge::LuaException &) {
        return false;
    }
    if (rigidbody == nullptr) return false;

    const b2Vec2 position = rigidbody->GetPosition();
    out_x = position.x;
    out_y = position.y;
    out_rotation = rigidbody->GetRotation();
    return true;
}

} // namespace

void SpriteRenderer::QueueDraw() const {
    if (!enabled || actor == nullptr || sprite.empty()) return;

    float world_x = 0.0f;
    float world_y = 0.0f;
    float rotation_degrees = 0.0f;

    // Prefer Transform when present so scene-authoring edits have one stable
    // source of truth. Fall back to Rigidbody for older gameplay scenes that
    // never authored a Transform component.
    if (!TryReadTransformState(actor, world_x, world_y, rotation_degrees) &&
        !TryReadRigidbodyState(actor, world_x, world_y, rotation_degrees)) {
        return;
    }

    ImageDrawRequest request;
    request.image_name = sprite;
    request.x = world_x;
    request.y = world_y;
    request.rotation_degrees = rotation_degrees;
    request.scale_x = scale_x;
    request.scale_y = scale_y;
    request.pivot_x = pivot_x;
    request.pivot_y = pivot_y;
    request.r = r;
    request.g = g;
    request.b = b;
    request.a = a;
    request.sorting_order = auto_sorting_order ? static_cast<int>(world_y) : sorting_order;
    Renderer::QueueSceneImageDraw(request);
}
