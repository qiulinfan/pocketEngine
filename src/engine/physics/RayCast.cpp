#include "physics/RayCast.h"
#include <algorithm>
#include <unordered_map>

namespace {

struct RaycastHitCandidate {
    RayCast::HitResult hit;
    float fraction = 1.0f;
};

b2World *g_physics_world = nullptr;
std::unordered_map<const b2Fixture *, RayCast::FixtureMetadata> g_fixture_metadata;

class CollectingRayCastCallback : public b2RayCastCallback {
public:
    explicit CollectingRayCastCallback(std::vector<RaycastHitCandidate> &hits)
        : hits_(hits) {}

    float ReportFixture(b2Fixture *fixture, const b2Vec2 &point,
                        const b2Vec2 &normal, float fraction) override {
        const RayCast::FixtureMetadata *metadata = RayCast::FindFixtureMetadata(fixture);
        if (metadata == nullptr) return -1.0f;
        if (metadata->actor == nullptr) return -1.0f;
        if (metadata->kind == RayCast::FixtureKind::Phantom) return -1.0f;

        RaycastHitCandidate candidate;
        candidate.hit.actor = metadata->actor;
        candidate.hit.point = point;
        candidate.hit.normal = normal;
        candidate.hit.is_trigger =
            metadata->kind == RayCast::FixtureKind::Trigger;
        candidate.fraction = fraction;
        hits_.push_back(candidate);
        return 1.0f;
    }

private:
    std::vector<RaycastHitCandidate> &hits_;
};

std::vector<RaycastHitCandidate> CollectHits(const b2Vec2 &position,
                                             const b2Vec2 &direction,
                                             float distance) {
    std::vector<RaycastHitCandidate> hits;
    if (g_physics_world == nullptr || distance <= 0.0f) return hits;

    b2Vec2 normalized_direction = direction;
    if (normalized_direction.Normalize() == 0.0f) return hits;

    CollectingRayCastCallback callback(hits);
    const b2Vec2 end = position + (distance * normalized_direction);
    g_physics_world->RayCast(&callback, position, end);

    std::stable_sort(hits.begin(), hits.end(),
                     [](const RaycastHitCandidate &a,
                        const RaycastHitCandidate &b) {
                         return a.fraction < b.fraction;
                     });
    return hits;
}

} // namespace

namespace RayCast {

// global physics world accessors
b2World *GetPhysicsWorld() {
    return g_physics_world;
}

// global physics world accessors
void SetPhysicsWorld(b2World *world) {
    g_physics_world = world;
}

// global physics world accessors
void ClearPhysicsState() {
    g_fixture_metadata.clear();
    g_physics_world = nullptr;
}

// register and query fixture metadata
void RegisterFixture(b2Fixture *fixture, Actor *actor, FixtureKind kind) {
    if (fixture == nullptr) return;
    g_fixture_metadata[fixture] = {actor, kind};
}

// register and query fixture metadata
void UnregisterFixture(b2Fixture *fixture) {
    if (fixture == nullptr) return;
    g_fixture_metadata.erase(fixture);
}

// register and query fixture metadata
const FixtureMetadata *FindFixtureMetadata(const b2Fixture *fixture) {
    auto metadata_it = g_fixture_metadata.find(fixture);
    if (metadata_it == g_fixture_metadata.end()) return nullptr;
    return &metadata_it->second;
}

// nearest hit only
std::optional<HitResult> Cast(const b2Vec2 &position, const b2Vec2 &direction,
                              float distance) {
    const std::vector<RaycastHitCandidate> hits =
        CollectHits(position, direction, distance);
    if (hits.empty()) return std::nullopt;
    return hits.front().hit;
}

// return all hits, sorted by distance
std::vector<HitResult> CastAll(const b2Vec2 &position, const b2Vec2 &direction,
                               float distance) {
    const std::vector<RaycastHitCandidate> hit_candidates =
        CollectHits(position, direction, distance);

    std::vector<HitResult> hits;
    hits.reserve(hit_candidates.size());
    for (const RaycastHitCandidate &candidate : hit_candidates) {
        hits.push_back(candidate.hit);
    }
    return hits;
}

} // namespace RayCast
