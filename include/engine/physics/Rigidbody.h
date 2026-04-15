#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "box2d/box2d.h"
#include <string>

struct Actor;

class Rigidbody {
public:
    Rigidbody() = default;
    Rigidbody(const Rigidbody &) = default;

    Actor *actor = nullptr;
    std::string key = "";
    bool enabled = true;

    // initial transform and body config
    // 初始变换和刚体配置
    float x = 0.0f;
    float y = 0.0f;
    std::string body_type = "dynamic";
    // Runtime-only physics hierarchy override. Authoring data continues to live
    // in `body_type`; this field only controls how the live Box2D body behaves.
    std::string effective_body_type = "";
    bool precise = true;
    float gravity_scale = 1.0f;
    float density = 1.0f;
    float angular_friction = 0.3f;
    float rotation = 0.0f;

    // collider and trigger settings
    // collider 和 trigger 配置
    bool has_collider = true;
    bool has_trigger = true;
    std::string collider_type = "box";
    float width = 1.0f;
    float height = 1.0f;
    float radius = 0.5f;
    std::string trigger_type = "box";
    float trigger_width = 1.0f;
    float trigger_height = 1.0f;
    float trigger_radius = 0.5f;
    float friction = 0.3f;
    float bounciness = 0.3f;

    // backing Box2D body pointer
    // 底层 Box2D body 指针
    b2Body *body = nullptr;

    void OnStart();
    void OnDestroy();

    // runtime physics controls
    // 运行时物理控制接口
    void AddForce(const b2Vec2 &force);
    void SetVelocity(const b2Vec2 &velocity);
    void SetPosition(const b2Vec2 &position);
    void SetRotation(float degrees_clockwise);
    void SetEffectiveBodyType(const std::string &type_name);
    void SetAngularVelocity(float degrees_clockwise);
    void SetGravityScale(float scale);
    void SetUpDirection(b2Vec2 direction);
    void SetRightDirection(b2Vec2 direction);

    b2Vec2 GetPosition() const;
    float GetRotation() const;
    std::string GetEffectiveBodyType() const;
    b2Vec2 GetVelocity() const;
    float GetAngularVelocity() const;
    float GetGravityScale() const;
    b2Vec2 GetUpDirection() const;
    b2Vec2 GetRightDirection() const;

    // global physics world lifecycle
    // 全局物理世界生命周期
    static void EnsurePhysicsWorld();
    static void DestroyPhysicsWorld();
    static bool HasPhysicsWorld();
    static void StepPhysicsWorld();
};

#endif
