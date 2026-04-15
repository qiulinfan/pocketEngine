#ifndef SHARED_PHYSICS_PHYSICS_HIERARCHY_STATE_H
#define SHARED_PHYSICS_PHYSICS_HIERARCHY_STATE_H

#include <cstdint>
#include <string>

namespace PhysicsHierarchy {

inline constexpr std::uint64_t kInvalidActorUID = 0;
inline constexpr const char *kNoBodyType = "none";

/*
Read-only physics hierarchy classification derived from:
- whether this actor owns a Rigidbody
- whether that Rigidbody currently requests dynamic/kinematic/static
- whether an ancestor already owns the nearest active dynamic Rigidbody

This is intentionally derived state, not serialized actor data.
*/
struct State {
    bool has_rigidbody_self = false;
    bool rigidbody_enabled_self = false;
    bool has_dynamic_rigidbody_self = false;
    std::string rigidbody_component_key;
    std::string requested_body_type = kNoBodyType;
    std::string effective_body_type = kNoBodyType;
    std::uint64_t nearest_dynamic_body_ancestor_uid = kInvalidActorUID;
    std::uint64_t physics_root_uid = kInvalidActorUID;
    bool is_under_dynamic_hierarchy = false;
};

} // namespace PhysicsHierarchy

#endif
