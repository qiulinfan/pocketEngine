#ifndef RAYCAST_H
#define RAYCAST_H

#include "box2d/box2d.h"
#include <optional>
#include <vector>

struct Actor;

namespace RayCast {

struct HitResult {
    Actor *actor = nullptr;
    b2Vec2 point = b2Vec2(0.0f, 0.0f);
    b2Vec2 normal = b2Vec2(0.0f, 0.0f);
    bool is_trigger = false;
};

enum class FixtureKind {
    // normal collider fixture
    // 普通碰撞体
    Collider,

    // sensor-only trigger fixture
    // 仅触发事件的 trigger
    Trigger,

    // placeholder fixture, ignored by raycast hit results
    // 占位 fixture, 不会作为 raycast 命中结果返回
    Phantom,
};

struct FixtureMetadata {
    // owning actor and logical fixture type
    // 所属 actor 和逻辑 fixture 类型
    Actor *actor = nullptr;
    FixtureKind kind = FixtureKind::Collider;
};

// global physics world accessors
// 全局物理世界访问接口
b2World *GetPhysicsWorld();
void SetPhysicsWorld(b2World *world);
void ClearPhysicsState();

// register and query fixture metadata
// 注册和查询 fixture 元数据
void RegisterFixture(b2Fixture *fixture, Actor *actor, FixtureKind kind);
void UnregisterFixture(b2Fixture *fixture);
const FixtureMetadata *FindFixtureMetadata(const b2Fixture *fixture);

// nearest hit only
// 只返回最近命中
std::optional<HitResult> Cast(const b2Vec2 &position, const b2Vec2 &direction,
                              float distance);

// return all hits, sorted by distance
// 返回所有命中结果, 并按距离排序
std::vector<HitResult> CastAll(const b2Vec2 &position, const b2Vec2 &direction,
                               float distance);

} // namespace RayCast

#endif
