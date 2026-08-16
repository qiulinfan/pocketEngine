#include "RegistrationDetail.h"
#include "particles/ParticleSystem.h"
#include "physics/Rigidbody.h"
#include "rendering/SpriteRenderer.h"
#include "rendering/MeshRenderer.h"
#include "scene/Transform.h"
#include "scene/Transform3D.h"
#include "scene/Camera3D.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"

namespace {

using APIRegistrationDetail::g_lua_state;

void InjectRigidbodyAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<Rigidbody>("Rigidbody")
        .addConstructor<void (*)()>()
        .addProperty("actor", &Rigidbody::actor)
        .addProperty("key", &Rigidbody::key)
        .addProperty("enabled", &Rigidbody::enabled)
        .addProperty("x", &Rigidbody::x)
        .addProperty("y", &Rigidbody::y)
        .addProperty("body_type", &Rigidbody::body_type)
        .addProperty("precise", &Rigidbody::precise)
        .addProperty("gravity_scale", &Rigidbody::gravity_scale)
        .addProperty("density", &Rigidbody::density)
        .addProperty("angular_friction", &Rigidbody::angular_friction)
        .addProperty("rotation", &Rigidbody::rotation)
        .addProperty("has_collider", &Rigidbody::has_collider)
        .addProperty("has_trigger", &Rigidbody::has_trigger)
        .addProperty("collider_type", &Rigidbody::collider_type)
        .addProperty("width", &Rigidbody::width)
        .addProperty("height", &Rigidbody::height)
        .addProperty("radius", &Rigidbody::radius)
        .addProperty("trigger_type", &Rigidbody::trigger_type)
        .addProperty("trigger_width", &Rigidbody::trigger_width)
        .addProperty("trigger_height", &Rigidbody::trigger_height)
        .addProperty("trigger_radius", &Rigidbody::trigger_radius)
        .addProperty("friction", &Rigidbody::friction)
        .addProperty("bounciness", &Rigidbody::bounciness)
        .addFunction("OnStart", &Rigidbody::OnStart)
        .addFunction("OnDestroy", &Rigidbody::OnDestroy)
        .addFunction("AddForce", &Rigidbody::AddForce)
        .addFunction("SetVelocity", &Rigidbody::SetVelocity)
        .addFunction("SetPosition", &Rigidbody::SetPosition)
        .addFunction("SetRotation", &Rigidbody::SetRotation)
        .addFunction("SetAngularVelocity", &Rigidbody::SetAngularVelocity)
        .addFunction("SetGravityScale", &Rigidbody::SetGravityScale)
        .addFunction("SetUpDirection", &Rigidbody::SetUpDirection)
        .addFunction("SetRightDirection", &Rigidbody::SetRightDirection)
        .addFunction("GetPosition", &Rigidbody::GetPosition)
        .addFunction("GetRotation", &Rigidbody::GetRotation)
        .addFunction("GetVelocity", &Rigidbody::GetVelocity)
        .addFunction("GetAngularVelocity", &Rigidbody::GetAngularVelocity)
        .addFunction("GetGravityScale", &Rigidbody::GetGravityScale)
        .addFunction("GetUpDirection", &Rigidbody::GetUpDirection)
        .addFunction("GetRightDirection", &Rigidbody::GetRightDirection)
        .endClass();
}

void InjectParticleSystemAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<ParticleSystem>("ParticleSystem")
        .addConstructor<void (*)()>()
        .addProperty("actor", &ParticleSystem::actor)
        .addProperty("key", &ParticleSystem::key)
        .addProperty("enabled", &ParticleSystem::enabled)
        .addProperty("x", &ParticleSystem::x)
        .addProperty("y", &ParticleSystem::y)
        .addProperty("frames_between_bursts",
                     &ParticleSystem::frames_between_bursts)
        .addProperty("burst_quantity", &ParticleSystem::burst_quantity)
        .addProperty("duration_frames", &ParticleSystem::duration_frames)
        .addProperty("start_scale_min", &ParticleSystem::start_scale_min)
        .addProperty("start_scale_max", &ParticleSystem::start_scale_max)
        .addProperty("start_speed_min", &ParticleSystem::start_speed_min)
        .addProperty("start_speed_max", &ParticleSystem::start_speed_max)
        .addProperty("rotation_min", &ParticleSystem::rotation_min)
        .addProperty("rotation_max", &ParticleSystem::rotation_max)
        .addProperty("rotation_speed_min",
                     &ParticleSystem::rotation_speed_min)
        .addProperty("rotation_speed_max",
                     &ParticleSystem::rotation_speed_max)
        .addProperty("start_color_r", &ParticleSystem::start_color_r)
        .addProperty("start_color_g", &ParticleSystem::start_color_g)
        .addProperty("start_color_b", &ParticleSystem::start_color_b)
        .addProperty("start_color_a", &ParticleSystem::start_color_a)
        .addProperty("end_color_r", &ParticleSystem::end_color_r)
        .addProperty("end_color_g", &ParticleSystem::end_color_g)
        .addProperty("end_color_b", &ParticleSystem::end_color_b)
        .addProperty("end_color_a", &ParticleSystem::end_color_a)
        .addProperty("emit_radius_min", &ParticleSystem::emit_radius_min)
        .addProperty("emit_radius_max", &ParticleSystem::emit_radius_max)
        .addProperty("emit_angle_min", &ParticleSystem::emit_angle_min)
        .addProperty("emit_angle_max", &ParticleSystem::emit_angle_max)
        .addProperty("gravity_scale_x", &ParticleSystem::gravity_scale_x)
        .addProperty("gravity_scale_y", &ParticleSystem::gravity_scale_y)
        .addProperty("drag_factor", &ParticleSystem::drag_factor)
        .addProperty("angular_drag_factor",
                     &ParticleSystem::angular_drag_factor)
        .addProperty("end_scale", &ParticleSystem::end_scale)
        .addProperty("image", &ParticleSystem::image)
        .addProperty("sorting_order", &ParticleSystem::sorting_order)
        .addFunction("OnStart", &ParticleSystem::OnStart)
        .addFunction("OnUpdate", &ParticleSystem::OnUpdate)
        .addFunction("OnDestroy", &ParticleSystem::OnDestroy)
        .addFunction("Stop", &ParticleSystem::Stop)
        .addFunction("Play", &ParticleSystem::Play)
        .addFunction("Burst", &ParticleSystem::Burst)
        .endClass();
}

void InjectTransformAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<Transform>("Transform")
        .addConstructor<void (*)()>()
        .addProperty("actor", &Transform::actor)
        .addProperty("key", &Transform::key)
        .addProperty("enabled", &Transform::enabled)
        .addProperty("x", &Transform::x)
        .addProperty("y", &Transform::y)
        .addProperty("rotation", &Transform::rotation)
        .endClass();
}

void InjectSpriteRendererAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<SpriteRenderer>("SpriteRenderer")
        .addConstructor<void (*)()>()
        .addProperty("actor", &SpriteRenderer::actor)
        .addProperty("key", &SpriteRenderer::key)
        .addProperty("enabled", &SpriteRenderer::enabled)
        .addProperty("sprite", &SpriteRenderer::sprite)
        .addProperty("r", &SpriteRenderer::r)
        .addProperty("g", &SpriteRenderer::g)
        .addProperty("b", &SpriteRenderer::b)
        .addProperty("a", &SpriteRenderer::a)
        .addProperty("pivot_x", &SpriteRenderer::pivot_x)
        .addProperty("pivot_y", &SpriteRenderer::pivot_y)
        .addProperty("scale_x", &SpriteRenderer::scale_x)
        .addProperty("scale_y", &SpriteRenderer::scale_y)
        .addProperty("sprite_row", &SpriteRenderer::sprite_row)
        .addProperty("sprite_column", &SpriteRenderer::sprite_column)
        .addProperty("sorting_order", &SpriteRenderer::sorting_order)
        .addProperty("auto_sorting_order", &SpriteRenderer::auto_sorting_order)
        .addFunction("SetSpriteCell", &SpriteRenderer::SetSpriteCell)
        .endClass();
}

void InjectTransform3DAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<Transform3D>("Transform3D")
        .addConstructor<void (*)()>()
        .addProperty("actor", &Transform3D::actor)
        .addProperty("key", &Transform3D::key)
        .addProperty("enabled", &Transform3D::enabled)
        .addProperty("position_x", &Transform3D::position_x)
        .addProperty("position_y", &Transform3D::position_y)
        .addProperty("position_z", &Transform3D::position_z)
        .addProperty("rotation_x", &Transform3D::rotation_x)
        .addProperty("rotation_y", &Transform3D::rotation_y)
        .addProperty("rotation_z", &Transform3D::rotation_z)
        .addProperty("rotation_w", &Transform3D::rotation_w)
        .addProperty("scale_x", &Transform3D::scale_x)
        .addProperty("scale_y", &Transform3D::scale_y)
        .addProperty("scale_z", &Transform3D::scale_z)
        .addFunction("NormalizeRotation", &Transform3D::NormalizeRotation)
        .endClass();
}

void InjectCamera3DAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<Camera3D>("Camera3D")
        .addConstructor<void (*)()>()
        .addProperty("actor", &Camera3D::actor)
        .addProperty("key", &Camera3D::key)
        .addProperty("enabled", &Camera3D::enabled)
        .addProperty("primary", &Camera3D::primary)
        .addProperty("orthographic", &Camera3D::orthographic)
        .addProperty("vertical_fov_degrees", &Camera3D::vertical_fov_degrees)
        .addProperty("near_clip", &Camera3D::near_clip)
        .addProperty("far_clip", &Camera3D::far_clip)
        .addProperty("orthographic_height", &Camera3D::orthographic_height)
        .endClass();
}

void InjectMeshRendererAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginClass<MeshRenderer>("MeshRenderer")
        .addConstructor<void (*)()>()
        .addProperty("actor", &MeshRenderer::actor)
        .addProperty("key", &MeshRenderer::key)
        .addProperty("enabled", &MeshRenderer::enabled)
        .addProperty("mesh", &MeshRenderer::mesh)
        .addProperty("color_r", &MeshRenderer::color_r)
        .addProperty("color_g", &MeshRenderer::color_g)
        .addProperty("color_b", &MeshRenderer::color_b)
        .addProperty("color_a", &MeshRenderer::color_a)
        .endClass();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterBuiltinComponentAPI() {
    InjectTransformAPI();
    InjectTransform3DAPI();
    InjectCamera3DAPI();
    InjectMeshRendererAPI();
    InjectSpriteRendererAPI();
    InjectRigidbodyAPI();
    InjectParticleSystemAPI();
}

} // namespace APIRegistrationDetail
