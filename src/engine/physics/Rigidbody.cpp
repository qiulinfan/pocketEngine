#include "physics/Rigidbody.h"
#include "scene/Actor.h"
#include "scripting/ComponentManager.h"
#include "physics/RayCast.h"
#include "glm/glm.hpp"

namespace {

class RigidbodyContactListener : public b2ContactListener {
public:
    void BeginContact(b2Contact *contact) override {
        QueueContactEvents(contact, true);
    }

    void EndContact(b2Contact *contact) override {
        QueueContactEvents(contact, false);
    }

private:
    static void QueueContactEvents(b2Contact *contact, bool is_enter) {
        if (contact == nullptr) return;

        b2Fixture *fixture_a = contact->GetFixtureA();
        b2Fixture *fixture_b = contact->GetFixtureB();
        if (fixture_a == nullptr || fixture_b == nullptr) return;

        const RayCast::FixtureMetadata *metadata_a =
            RayCast::FindFixtureMetadata(fixture_a);
        const RayCast::FixtureMetadata *metadata_b =
            RayCast::FindFixtureMetadata(fixture_b);
        if (metadata_a == nullptr || metadata_b == nullptr) return;
        if (metadata_a->actor == nullptr || metadata_b->actor == nullptr) return;

        const b2Vec2 velocity_a = fixture_a->GetBody()->GetLinearVelocity();
        const b2Vec2 velocity_b = fixture_b->GetBody()->GetLinearVelocity();
        const b2Vec2 relative_velocity = velocity_a - velocity_b;

        if (metadata_a->kind == RayCast::FixtureKind::Collider &&
            metadata_b->kind == RayCast::FixtureKind::Collider) {
            b2Vec2 point(-999.0f, -999.0f);
            b2Vec2 normal(-999.0f, -999.0f);
            if (is_enter) {
                b2WorldManifold world_manifold;
                contact->GetWorldManifold(&world_manifold);
                point = world_manifold.points[0];
                normal = world_manifold.normal;
            }

            ComponentManager::QueueCollisionEvent(
                metadata_a->actor, metadata_b->actor, point, relative_velocity,
                normal, is_enter);
            ComponentManager::QueueCollisionEvent(
                metadata_b->actor, metadata_a->actor, point, relative_velocity,
                normal, is_enter);
            return;
        }

        if (metadata_a->kind == RayCast::FixtureKind::Trigger &&
            metadata_b->kind == RayCast::FixtureKind::Trigger) {
            const b2Vec2 sentinel(-999.0f, -999.0f);
            ComponentManager::QueueTriggerEvent(
                metadata_a->actor, metadata_b->actor, sentinel, relative_velocity,
                sentinel, is_enter);
            ComponentManager::QueueTriggerEvent(
                metadata_b->actor, metadata_a->actor, sentinel, relative_velocity,
                sentinel, is_enter);
        }
    }
};

RigidbodyContactListener g_contact_listener;

float ClockwiseDegreesToBox2DRadians(float rotation_degrees) {
    return rotation_degrees * (b2_pi / 180.0f);
}

float Box2DRadiansToClockwiseDegrees(float rotation_radians) {
    return rotation_radians * (180.0f / b2_pi);
}

b2BodyType ParseBodyType(const std::string &body_type) {
    if (body_type == "static") return b2_staticBody;
    if (body_type == "kinematic") return b2_kinematicBody;
    return b2_dynamicBody;
}

void CreateColliderFixture(Rigidbody &rigidbody) {
    if (rigidbody.body == nullptr || !rigidbody.has_collider) return;

    b2FixtureDef fixture_def;
    fixture_def.isSensor = false;
    fixture_def.density = rigidbody.density;
    fixture_def.friction = rigidbody.friction;
    fixture_def.restitution = rigidbody.bounciness;

    if (rigidbody.collider_type == "circle") {
        b2CircleShape shape;
        shape.m_radius = rigidbody.radius;
        fixture_def.shape = &shape;
        b2Fixture *fixture = rigidbody.body->CreateFixture(&fixture_def);
        RayCast::RegisterFixture(fixture, rigidbody.actor,
                                 RayCast::FixtureKind::Collider);
        return;
    }

    b2PolygonShape shape;
    shape.SetAsBox(rigidbody.width * 0.5f, rigidbody.height * 0.5f);
    fixture_def.shape = &shape;
    b2Fixture *fixture = rigidbody.body->CreateFixture(&fixture_def);
    RayCast::RegisterFixture(fixture, rigidbody.actor,
                             RayCast::FixtureKind::Collider);
}


void CreateTriggerFixture(Rigidbody &rigidbody) {
    if (rigidbody.body == nullptr || !rigidbody.has_trigger) return;

    b2FixtureDef trigger_fixture_def;
    trigger_fixture_def.isSensor = true;
    trigger_fixture_def.density = rigidbody.density;

    if (rigidbody.trigger_type == "circle") {
        b2CircleShape shape;
        shape.m_radius = rigidbody.trigger_radius;
        trigger_fixture_def.shape = &shape;
        b2Fixture *fixture = rigidbody.body->CreateFixture(&trigger_fixture_def);
        RayCast::RegisterFixture(fixture, rigidbody.actor,
                                 RayCast::FixtureKind::Trigger);
        return;
    }

    b2PolygonShape shape;
    shape.SetAsBox(rigidbody.trigger_width * 0.5f,
                   rigidbody.trigger_height * 0.5f);
    trigger_fixture_def.shape = &shape;
    b2Fixture *fixture = rigidbody.body->CreateFixture(&trigger_fixture_def);
    RayCast::RegisterFixture(fixture, rigidbody.actor,
                             RayCast::FixtureKind::Trigger);
}

void CreatePhantomFixture(Rigidbody &rigidbody) {
    if (rigidbody.body == nullptr || rigidbody.has_collider || rigidbody.has_trigger) {
        return;
    }

    b2PolygonShape phantom_shape;
    phantom_shape.SetAsBox(rigidbody.width * 0.5f, rigidbody.height * 0.5f);

    b2FixtureDef phantom_fixture_def;
    phantom_fixture_def.shape = &phantom_shape;
    phantom_fixture_def.density = rigidbody.density;
    phantom_fixture_def.isSensor = true;
    b2Fixture *fixture = rigidbody.body->CreateFixture(&phantom_fixture_def);
    RayCast::RegisterFixture(fixture, rigidbody.actor,
                             RayCast::FixtureKind::Phantom);
}

} // namespace

// global physics world lifecycle
void Rigidbody::EnsurePhysicsWorld() {
    if (RayCast::GetPhysicsWorld() != nullptr) return;
    b2World *world = new b2World(b2Vec2(0.0f, 9.8f));
    world->SetContactListener(&g_contact_listener);
    RayCast::SetPhysicsWorld(world);
}

// global physics world lifecycle
void Rigidbody::DestroyPhysicsWorld() {
    delete RayCast::GetPhysicsWorld();
    RayCast::ClearPhysicsState();
}

// global physics world lifecycle
bool Rigidbody::HasPhysicsWorld() {
    return RayCast::GetPhysicsWorld() != nullptr;
}

// global physics world lifecycle
void Rigidbody::StepPhysicsWorld() {
    b2World *world = RayCast::GetPhysicsWorld();
    if (world == nullptr) return;
    world->Step(1.0f / 60.0f, 8, 3);
}

// Create the Box2D body and fixtures from the serialized runtime config.
void Rigidbody::OnStart() {
    if (body != nullptr) return;

    EnsurePhysicsWorld();

    b2BodyDef body_def;
    body_def.type = ParseBodyType(GetEffectiveBodyType());
    body_def.position.Set(x, y);
    body_def.bullet = precise;
    body_def.gravityScale = gravity_scale;
    body_def.angularDamping = angular_friction;
    body_def.angle = ClockwiseDegreesToBox2DRadians(rotation);

    body = RayCast::GetPhysicsWorld()->CreateBody(&body_def);

    CreateColliderFixture(*this);
    CreateTriggerFixture(*this);
    CreatePhantomFixture(*this);
}

// Destroy the backing Box2D body and unregister all fixture metadata.
void Rigidbody::OnDestroy() {
    b2World *world = RayCast::GetPhysicsWorld();
    if (body == nullptr || world == nullptr) return;

    for (b2Fixture *fixture = body->GetFixtureList(); fixture != nullptr;
         fixture = fixture->GetNext()) {
        RayCast::UnregisterFixture(fixture);
    }
    world->DestroyBody(body);
    body = nullptr;
}

// runtime physics controls
b2Vec2 Rigidbody::GetPosition() const {
    if (body != nullptr) return body->GetPosition();
    return b2Vec2(x, y);
}

// runtime physics controls
float Rigidbody::GetRotation() const {
    if (body != nullptr) {
        return Box2DRadiansToClockwiseDegrees(body->GetAngle());
    }
    return rotation;
}

std::string Rigidbody::GetEffectiveBodyType() const {
    if (!effective_body_type.empty() && effective_body_type != "none") {
        return effective_body_type;
    }
    return body_type;
}

// runtime physics controls
void Rigidbody::AddForce(const b2Vec2 &force) {
    if (body == nullptr) return;
    body->ApplyForceToCenter(force, true);
}

// runtime physics controls
void Rigidbody::SetVelocity(const b2Vec2 &velocity) {
    if (body == nullptr) return;
    body->SetLinearVelocity(velocity);
}

// runtime physics controls
void Rigidbody::SetPosition(const b2Vec2 &position) {
    x = position.x;
    y = position.y;

    if (body == nullptr) return;
    body->SetTransform(position, body->GetAngle());
}

// runtime physics controls
void Rigidbody::SetRotation(float degrees_clockwise) {
    rotation = degrees_clockwise;

    if (body == nullptr) return;
    body->SetTransform(body->GetPosition(),
                       ClockwiseDegreesToBox2DRadians(degrees_clockwise));
}

void Rigidbody::SetEffectiveBodyType(const std::string &type_name) {
    effective_body_type = type_name;
    if (body == nullptr) return;
    if (effective_body_type.empty() || effective_body_type == "none") return;
    body->SetType(ParseBodyType(GetEffectiveBodyType()));
}

// runtime physics controls
void Rigidbody::SetAngularVelocity(float degrees_clockwise) {
    if (body == nullptr) return;
    body->SetAngularVelocity(ClockwiseDegreesToBox2DRadians(degrees_clockwise));
}

// runtime physics controls
void Rigidbody::SetGravityScale(float scale) {
    gravity_scale = scale;
    if (body == nullptr) return;
    body->SetGravityScale(scale);
}

// runtime physics controls
void Rigidbody::SetUpDirection(b2Vec2 direction) {
    if (direction.Normalize() == 0.0f) return;

    const float new_angle_radians = glm::atan(direction.x, -direction.y);
    rotation = Box2DRadiansToClockwiseDegrees(new_angle_radians);

    if (body == nullptr) return;
    body->SetTransform(body->GetPosition(), new_angle_radians);
}

// runtime physics controls
void Rigidbody::SetRightDirection(b2Vec2 direction) {
    if (direction.Normalize() == 0.0f) return;

    const float new_angle_radians =
        glm::atan(direction.x, -direction.y) - (b2_pi / 2.0f);
    rotation = Box2DRadiansToClockwiseDegrees(new_angle_radians);

    if (body == nullptr) return;
    body->SetTransform(body->GetPosition(), new_angle_radians);
}

// runtime physics controls
b2Vec2 Rigidbody::GetVelocity() const {
    if (body != nullptr) return body->GetLinearVelocity();
    return b2Vec2(0.0f, 0.0f);
}

// runtime physics controls
float Rigidbody::GetAngularVelocity() const {
    if (body != nullptr) {
        return Box2DRadiansToClockwiseDegrees(body->GetAngularVelocity());
    }
    return 0.0f;
}

// runtime physics controls
float Rigidbody::GetGravityScale() const {
    if (body != nullptr) return body->GetGravityScale();
    return gravity_scale;
}

// runtime physics controls
b2Vec2 Rigidbody::GetUpDirection() const {
    const float angle = (body != nullptr) ? body->GetAngle()
                                          : ClockwiseDegreesToBox2DRadians(rotation);
    b2Vec2 result(glm::sin(angle), -glm::cos(angle));
    result.Normalize();
    return result;
}

// runtime physics controls
b2Vec2 Rigidbody::GetRightDirection() const {
    const float angle = (body != nullptr) ? body->GetAngle()
                                          : ClockwiseDegreesToBox2DRadians(rotation);
    b2Vec2 result(glm::cos(angle), glm::sin(angle));
    result.Normalize();
    return result;
}
