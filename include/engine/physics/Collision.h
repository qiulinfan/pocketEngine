#ifndef COLLISION_H
#define COLLISION_H

#include "box2d/box2d.h"

struct Actor;

struct Collision {
    // other actor involved in this contact
    // 与当前 actor 接触的另一个 actor
    Actor *other = nullptr;

    // world-space contact point. trigger events may use a sentinel value
    // 世界坐标下的接触点. trigger 事件可能会用哨兵值
    b2Vec2 point = b2Vec2(0.0f, 0.0f);

    // relative velocity at the contact
    // 接触时的相对速度
    b2Vec2 relative_velocity = b2Vec2(0.0f, 0.0f);

    // contact normal. trigger events may not have a real normal
    // 接触法线. trigger 事件可能没有真实法线
    b2Vec2 normal = b2Vec2(0.0f, 0.0f);
};

#endif
