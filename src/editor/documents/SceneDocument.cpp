#include "editor/documents/SceneDocument.h"
#include "scene/Actor.h"
#include "shared/resources/ResourcePath.h"
#include "scripting/ComponentManager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>

namespace {

std::string BuildFallbackActorName(std::size_t actor_index) {
    std::ostringstream label;
    label << "Actor " << (actor_index + 1);
    return label.str();
}

bool TryReadPropertyAsFloat(const std::vector<Actor::ComponentProperty> &properties,
                            const std::string &property_name,
                            float &out_value) {
    for (const Actor::ComponentProperty &property : properties) {
        if (property.name != property_name) continue;
        if (const int *typed_value = std::get_if<int>(&property.value)) {
            out_value = static_cast<float>(*typed_value);
            return true;
        }
        if (const double *typed_value = std::get_if<double>(&property.value)) {
            out_value = static_cast<float>(*typed_value);
            return true;
        }
        return false;
    }
    return false;
}

bool TryReadPropertyAsBool(const std::vector<Actor::ComponentProperty> &properties,
                           const std::string &property_name,
                           bool &out_value) {
    for (const Actor::ComponentProperty &property : properties) {
        if (property.name != property_name) continue;
        if (const bool *typed_value = std::get_if<bool>(&property.value)) {
            out_value = *typed_value;
            return true;
        }
        return false;
    }
    return false;
}

bool TryReadPropertyAsString(const std::vector<Actor::ComponentProperty> &properties,
                             const std::string &property_name, std::string &out_value) {
    for (const Actor::ComponentProperty &property : properties) {
        if (property.name != property_name) continue;
        if (const std::string *typed_value =
                std::get_if<std::string>(&property.value)) {
            out_value = *typed_value;
            return true;
        }
        return false;
    }
    return false;
}

/*
Extract the actor-local Rigidbody request from the editor document cache.
Ancestry-derived fields are resolved later by RebuildPhysicsHierarchyCache().
*/
PhysicsHierarchy::State BuildSceneActorPhysicsSelfState(const SceneDocument &scene_document, std::size_t actor_index) {
    PhysicsHierarchy::State state;
    const Actor effective_actor = scene_document.BuildEffectiveActor(actor_index);
    for (const Actor::ComponentSpec &component_spec :
         effective_actor.component_specs) {
        if (component_spec.type != "Rigidbody") continue;

        state.has_rigidbody_self = true;
        state.rigidbody_component_key = component_spec.key;

        const std::vector<Actor::ComponentProperty> properties =
            scene_document.GetInspectableProperties(actor_index,
                                                    component_spec.key);
        bool enabled = true;
        TryReadPropertyAsBool(properties, "enabled", enabled);
        state.rigidbody_enabled_self = enabled;

        std::string requested_body_type = "dynamic";
        TryReadPropertyAsString(properties, "body_type", requested_body_type);
        state.requested_body_type = requested_body_type;
        state.has_dynamic_rigidbody_self = enabled && requested_body_type == "dynamic";
        state.effective_body_type = enabled ? requested_body_type : PhysicsHierarchy::kNoBodyType;
        break;
    }
    return state;
}

void RotateClockwise(float x, float y, float rotation_degrees,
                     float &out_x, float &out_y) {
    const float radians =
        rotation_degrees * (3.14159265358979323846f / 180.0f);
    const float cos_theta = std::cos(radians);
    const float sin_theta = std::sin(radians);
    out_x = cos_theta * x + sin_theta * y;
    out_y = -sin_theta * x + cos_theta * y;
}

} // namespace

/* --------------------------------------------------------------------------
SceneDocument public API: disk I/O
-------------------------------------------------------------------------- */

bool SceneDocument::LoadFromSceneName(const std::string &scene_name) {
    /*
    This is the only place the editor document touches disk for loading.
    After this point, all edits target the in-memory cache below.
    */
    scene_asset_ = SceneFormat::LoadSceneAsset(scene_name);
    if (scene_asset_.scene_path.empty()) {
        actor_uids_.clear();
        ResetSceneBackedUIDAllocator();
        ResetNewActorCounterState();
        return false;
    }

    ReassignFreshActorUIDs();
    InvalidateHierarchyCache();
    dirty_ = false;
    LoadPersistedNewActorCounter();
    return true;
}

bool SceneDocument::LoadFromScenePath(const std::filesystem::path &scene_path) {
    /*
    Convert absolute/relative file path into the same scene-name +
    subdirectory pair used by runtime loading so both code paths stay
    consistent.
    */
    if (scene_path.empty()) {
        return false;
    }
    if (scene_path.extension() != ".scene") {
        return false;
    }

    const std::filesystem::path scene_root =
        ResourcePath::ResourceSubdirectory("scenes");
    const std::filesystem::path normalized_scene_path = scene_path.lexically_normal();

    std::error_code relative_error;
    const std::filesystem::path relative_scene_path =
        std::filesystem::relative(normalized_scene_path, scene_root,
                                  relative_error);
    if (relative_error || relative_scene_path.empty()) {
        return false;
    }

    const std::string relative_text = relative_scene_path.generic_string();
    if (relative_text.rfind("../", 0) == 0 || relative_text == "..") {
        return false;
    }

    const std::string scene_name = relative_scene_path.stem().string();
    if (scene_name.empty()) {
        return false;
    }

    const std::filesystem::path preferred_subdirectory =
        ResourcePath::NormalizeRelativePath(relative_scene_path.parent_path());
    scene_asset_ = SceneFormat::LoadSceneAsset(scene_name, preferred_subdirectory);
    if (scene_asset_.scene_path.empty()) {
        actor_uids_.clear();
        ResetSceneBackedUIDAllocator();
        ResetNewActorCounterState();
        return false;
    }

    ReassignFreshActorUIDs();
    InvalidateHierarchyCache();
    dirty_ = false;
    LoadPersistedNewActorCounter();
    return true;
}

bool SceneDocument::Save() {
    /*
    Saving flushes the editor cache back into the original source .scene
    file. Runtime stays a separate copy that will be refreshed explicitly.
    */
    if (!SceneFormat::SaveSceneAsset(scene_asset_)) return false;
    PersistNewActorCounter();
    dirty_ = false;
    return true;
}

/* --------------------------------------------------------------------------
SceneDocument public API: cache metadata / raw access
-------------------------------------------------------------------------- */

/* Return whether the editor cache diverged from disk contents. */
bool SceneDocument::IsDirty() const {
    return dirty_;
}

/* Return the scene name that was loaded from game.config / disk. */
const std::string &SceneDocument::GetSceneName() const {
    return scene_asset_.scene_name;
}

/* Return the resolved .scene source path on disk. */
const std::filesystem::path &SceneDocument::GetScenePath() const {
    return scene_asset_.scene_path;
}

/* Return the scene subdirectory relative to <project-root>/scenes. */
const std::filesystem::path &SceneDocument::GetSceneSubdirectory() const {
    return scene_asset_.scene_subdirectory;
}

/* Return immutable access to the raw actor cache. */
const std::vector<SceneDocument::ActorRecord> &SceneDocument::GetActorRecords() const {
    return scene_asset_.actors;
}

/*
Allocate one new stable UID for a scene-backed actor owned by this document.
We prefer the scene-backed range below the runtime-generated UID start, but we
can spill higher if a very large scene exhausts that preferred band.
*/
SceneDocument::ActorUID SceneDocument::AllocateNextSceneBackedActorUID(const std::unordered_set<ActorUID> &used_uids) {
    constexpr ActorUID kPreferredSceneActorUIDLimit = Actor::kRuntimeGeneratedUIDStart;

    const auto find_available_uid =
        [&](ActorUID begin_uid, ActorUID end_uid_exclusive) -> ActorUID {
        for (ActorUID candidate_uid = begin_uid;
             candidate_uid < end_uid_exclusive; ++candidate_uid) {
            if (candidate_uid == kInvalidActorUID) continue;
            if (used_uids.find(candidate_uid) != used_uids.end()) continue;
            return candidate_uid;
        }
        return kInvalidActorUID;
    };

    if (next_scene_backed_uid_ == kInvalidActorUID ||
        next_scene_backed_uid_ >= kPreferredSceneActorUIDLimit) {
        next_scene_backed_uid_ = 1;
    }

    ActorUID actor_uid = find_available_uid(next_scene_backed_uid_, kPreferredSceneActorUIDLimit);
    if (actor_uid == kInvalidActorUID) {
        /*
        If the cursor reached the end of the preferred scene-backed band, wrap
        around once and try to reuse holes left by deleted actors.
        */
        actor_uid = find_available_uid(1, kPreferredSceneActorUIDLimit);
        if (actor_uid == kInvalidActorUID) {
            /*
            Very large scenes may exhaust the preferred band entirely. In that
            case we spill upward rather than failing actor creation outright.
            */
            actor_uid = kPreferredSceneActorUIDLimit;
            while (used_uids.find(actor_uid) != used_uids.end()) {
                ++actor_uid;
            }
        }
    }

    next_scene_backed_uid_ = actor_uid + 1;
    return actor_uid;
}

void SceneDocument::ResetSceneBackedUIDAllocator() {
    next_scene_backed_uid_ = 1;
}

/* Return the shared scene asset that backs this document cache. */
const SceneFormat::SceneAsset &SceneDocument::GetSceneAsset() const {
    return scene_asset_;
}

/* Return the number of raw actor records currently stored in the cache. */
std::size_t SceneDocument::GetActorCount() const {
    return scene_asset_.actors.size();
}

/* Return the stable scene actor identity used for session/runtime mapping. */
SceneDocument::ActorUID SceneDocument::GetActorUID(std::size_t actor_index) const {
    if (actor_index >= actor_uids_.size()) return kInvalidActorUID;
    return actor_uids_[actor_index];
}

/* Find one cached actor index by its stable editor-only UID. */
std::optional<std::size_t> SceneDocument::FindActorIndexByUID( ActorUID actor_uid) const {
    if (actor_uid == kInvalidActorUID) return std::nullopt;

    for (std::size_t actor_index = 0; actor_index < actor_uids_.size();
         ++actor_index) {
        if (actor_uids_[actor_index] == actor_uid) {
            return actor_index;
        }
    }
    return std::nullopt;
}

/*
Return the derived parent index for one actor.
The raw scene only stores parent_uid, so this is served from the cached
UID -> index rebuild performed by RebuildHierarchyCache().
*/
std::optional<std::size_t> SceneDocument::FindParentActorIndex( std::size_t actor_index) const {
    RebuildHierarchyCache();
    if (actor_index >= parent_actor_indices_.size()) return std::nullopt;
    return parent_actor_indices_[actor_index];
}

/*
Return one actor's immediate children as a derived view over the flat actor
array. Children are not persisted directly in .scene to keep the source format
one-directional and easier to edit/repair.
*/
std::vector<std::size_t> SceneDocument::GetChildActorIndices( std::size_t actor_index) const {
    RebuildHierarchyCache();
    if (actor_index >= children_by_actor_index_.size()) return {};
    return children_by_actor_index_[actor_index];
}

/* Return the current root actors in hierarchy order. */
std::vector<std::size_t> SceneDocument::GetRootActorIndices() const {
    RebuildHierarchyCache();
    std::vector<std::size_t> root_indices;
    root_indices.reserve(scene_asset_.actors.size());
    for (std::size_t actor_index = 0; actor_index < scene_asset_.actors.size();
         ++actor_index) {
        if (!parent_actor_indices_[actor_index].has_value()) {
            root_indices.emplace_back(actor_index);
        }
    }
    return root_indices;
}

/*
Expose the derived physics-hierarchy classification for one scene actor. This
lets inspector/status show requested vs. effective Rigidbody ownership without
rewriting authored component data.
*/
PhysicsHierarchy::State SceneDocument::GetPhysicsHierarchyState( std::size_t actor_index) const {
    RebuildPhysicsHierarchyCache();
    if (actor_index >= physics_hierarchy_states_by_actor_index_.size()) {
        return {};
    }
    return physics_hierarchy_states_by_actor_index_[actor_index];
}

/* --------------------------------------------------------------------------
SceneDocument public API: derived runtime/effective views
-------------------------------------------------------------------------- */

/*
Build the effective actor view seen by inspector, scene picking, and runtime
mirroring. This merges template inheritance with locally-authored overrides.
*/
Actor SceneDocument::BuildEffectiveActor(std::size_t actor_index) const {
    const ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) {
        return Actor();
    }
    return SceneFormat::BuildEditableActor(*actor_record,
                                           scene_asset_.scene_subdirectory);
}

/* Return the display name shown in the hierarchy. */
std::string SceneDocument::GetActorDisplayName(std::size_t actor_index) const {
    const ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) return BuildFallbackActorName(actor_index);

    const Actor effective_actor = BuildEffectiveActor(actor_index);
    if (!effective_actor.actor_name.empty()) {
        return effective_actor.actor_name;
    }
    if (!actor_record->name.empty()) {
        return actor_record->name;
    }
    return BuildFallbackActorName(actor_index);
}

/*
Read the actor-local Transform component exactly as authored by this document's
effective component view. Local transform remains the source of truth that is
saved to disk and edited by inspector/scene drag.
*/
bool SceneDocument::TryGetActorLocalTransform(std::size_t actor_index,
                                              std::string *out_component_key,
                                              float &out_x, float &out_y,
                                              float &out_rotation) const {
    const Actor effective_actor = BuildEffectiveActor(actor_index);
    for (const Actor::ComponentSpec &component_spec : effective_actor.component_specs) {
        if (component_spec.type != "Transform") continue;

        const std::vector<Actor::ComponentProperty> properties =
            GetInspectableProperties(actor_index, component_spec.key);
        if (!TryReadPropertyAsFloat(properties, "x", out_x) ||
            !TryReadPropertyAsFloat(properties, "y", out_y)) {
            return false;
        }
        if (!TryReadPropertyAsFloat(properties, "rotation", out_rotation)) {
            out_rotation = 0.0f;
        }
        if (out_component_key != nullptr) {
            *out_component_key = component_spec.key;
        }
        return true;
    }
    return false;
}

/*
Resolve an actor's world transform on demand by walking parent links upward.
The editor does not persist world transforms; they are derived whenever scene
preview, selection, or reparenting needs them.
*/
bool SceneDocument::TryGetActorWorldTransform(std::size_t actor_index,
                                              std::string *out_component_key,
                                              float &out_x, float &out_y,
                                              float &out_rotation) const {
    if (actor_index >= scene_asset_.actors.size()) return false;

    if (out_component_key != nullptr) {
        float ignored_x = 0.0f;
        float ignored_y = 0.0f;
        float ignored_rotation = 0.0f;
        if (!TryGetActorLocalTransform(actor_index, out_component_key,
                                       ignored_x, ignored_y,
                                       ignored_rotation)) {
            return false;
        }
    }

    std::vector<bool> recursion_guard(scene_asset_.actors.size(), false);
    std::function<bool(std::size_t, float &, float &, float &)> resolve_world =
        [&](std::size_t current_actor_index, float &world_x, float &world_y,
            float &world_rotation) -> bool {
        if (current_actor_index >= scene_asset_.actors.size()) return false;
        if (recursion_guard[current_actor_index]) return false;
        recursion_guard[current_actor_index] = true;

        float local_x = 0.0f;
        float local_y = 0.0f;
        float local_rotation = 0.0f;
        if (!TryGetActorLocalTransform(current_actor_index, nullptr, local_x,
                                       local_y, local_rotation)) {
            recursion_guard[current_actor_index] = false;
            return false;
        }

        const std::optional<std::size_t> parent_actor_index = FindParentActorIndex(current_actor_index);
        if (!parent_actor_index.has_value()) {
            world_x = local_x;
            world_y = local_y;
            world_rotation = local_rotation;
            recursion_guard[current_actor_index] = false;
            return true;
        }

        float parent_world_x = 0.0f;
        float parent_world_y = 0.0f;
        float parent_world_rotation = 0.0f;
        if (!resolve_world(*parent_actor_index, parent_world_x, parent_world_y,
                           parent_world_rotation)) {
            /*
            Broken parent chains should not make the actor disappear from scene
            tools; we gracefully fall back to treating local as world.
            */
            world_x = local_x;
            world_y = local_y;
            world_rotation = local_rotation;
            recursion_guard[current_actor_index] = false;
            return true;
        }

        float rotated_local_x = 0.0f;
        float rotated_local_y = 0.0f;
        RotateClockwise(local_x, local_y, parent_world_rotation,
                        rotated_local_x, rotated_local_y);
        world_x = parent_world_x + rotated_local_x;
        world_y = parent_world_y + rotated_local_y;
        world_rotation = parent_world_rotation + local_rotation;
        recursion_guard[current_actor_index] = false;
        return true;
    };

    if (!resolve_world(actor_index, out_x, out_y, out_rotation)) {
        return false;
    }
    return true;
}

/*
Return editable properties for one component, including default scalar values
contributed by the component type table.
*/
std::vector<Actor::ComponentProperty> SceneDocument::GetInspectableProperties( std::size_t actor_index, const std::string &component_key) const {
    /*
    Inspector starts from the effective runtime-facing component view, then
    overlays script-declared defaults so users can edit both inherited and
    explicitly overridden scalar properties in one place.
    */
    std::vector<Actor::ComponentProperty> properties;
    const Actor effective_actor = BuildEffectiveActor(actor_index);
    const Actor::ComponentSpec *component_spec =
        SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       component_key);
    if (component_spec == nullptr) return properties;

    properties = ComponentManager::GetComponentTypeDefaultProperties(component_spec->type);
    for (const Actor::ComponentProperty &property : component_spec->overrides) {
        bool updated_existing = false;
        for (Actor::ComponentProperty &existing_property : properties) {
            if (existing_property.name != property.name) continue;
            existing_property.value = property.value;
            updated_existing = true;
            break;
        }
        if (!updated_existing) {
            properties.emplace_back(property);
        }
    }

    std::sort(properties.begin(), properties.end(),
              [](const Actor::ComponentProperty &a,
                 const Actor::ComponentProperty &b) {
                  return a.name < b.name;
              });
    return properties;
}

/* --------------------------------------------------------------------------
SceneDocument private helpers
-------------------------------------------------------------------------- */

SceneDocument::ActorRecord *SceneDocument::FindActorRecord( std::size_t actor_index) {
    if (actor_index >= scene_asset_.actors.size()) return nullptr;
    return &scene_asset_.actors[actor_index];
}

const SceneDocument::ActorRecord *SceneDocument::FindActorRecord( std::size_t actor_index) const {
    if (actor_index >= scene_asset_.actors.size()) return nullptr;
    return &scene_asset_.actors[actor_index];
}

Actor::ComponentSpec *SceneDocument::FindRawComponentSpec( std::size_t actor_index, const std::string &component_key) {
    ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) return nullptr;
    return SceneFormat::FindComponentSpec(actor_record->component_specs,
                                          component_key);
}

/*
Repair missing or duplicate scene-backed UIDs after load. This keeps old JSON
files compatible while preserving any valid identities already authored in the
scene file.
*/
void SceneDocument::ReassignFreshActorUIDs() {
    ResetSceneBackedUIDAllocator();
    actor_uids_.clear();
    actor_uids_.reserve(scene_asset_.actors.size());
    std::unordered_set<ActorUID> used_uids;
    used_uids.reserve(scene_asset_.actors.size());
    for (std::size_t actor_index = 0; actor_index < scene_asset_.actors.size();
         ++actor_index) {
        ActorUID actor_uid = scene_asset_.actors[actor_index].uid;
        if (actor_uid == kInvalidActorUID ||
            used_uids.find(actor_uid) != used_uids.end()) {
            actor_uid = AllocateNextSceneBackedActorUID(used_uids);
        }
        actor_uids_.emplace_back(actor_uid);
        used_uids.insert(actor_uid);
    }
    SyncActorUIDsIntoSceneAsset();

    /*
    New scene-backed actors should continue from the current document's
    highest known identity, not from a process-global static cursor.
    */
    if (used_uids.empty()) {
        next_scene_backed_uid_ = 1;
        return;
    }

    constexpr ActorUID kPreferredSceneActorUIDLimit = Actor::kRuntimeGeneratedUIDStart;
    ActorUID max_scene_uid = 0;
    for (ActorUID used_uid : used_uids) {
        max_scene_uid = std::max(max_scene_uid, used_uid);
    }
    next_scene_backed_uid_ =
        std::max<ActorUID>(1, std::min<ActorUID>(max_scene_uid + 1,
                                                 kPreferredSceneActorUIDLimit));
}

/* Push the repaired UID list back into the raw scene asset cache. */
void SceneDocument::SyncActorUIDsIntoSceneAsset() {
    const std::size_t actor_count = scene_asset_.actors.size();
    if (actor_uids_.size() < actor_count) {
        actor_uids_.resize(actor_count, kInvalidActorUID);
    } else if (actor_uids_.size() > actor_count) {
        actor_uids_.resize(actor_count);
    }

    for (std::size_t actor_index = 0; actor_index < actor_count; ++actor_index) {
        scene_asset_.actors[actor_index].uid = actor_uids_[actor_index];
    }
}

void SceneDocument::InvalidateHierarchyCache() {
    hierarchy_cache_dirty_ = true;
    InvalidatePhysicsHierarchyCache();
}

void SceneDocument::InvalidatePhysicsHierarchyCache() {
    physics_hierarchy_cache_dirty_ = true;
}

/*
Rebuild the derived parent/children view from the persisted parent_uid
links. The source scene asset stays flat; hierarchy is a cached interpretation.
*/
void SceneDocument::RebuildHierarchyCache() const {
    if (!hierarchy_cache_dirty_) return;

    const std::size_t actor_count = scene_asset_.actors.size();
    parent_actor_indices_.assign(actor_count, std::nullopt);
    children_by_actor_index_.assign(actor_count, {});

    for (std::size_t actor_index = 0; actor_index < actor_count; ++actor_index) {
        const ActorUID parent_uid = scene_asset_.actors[actor_index].parent_uid;
        if (parent_uid == kInvalidActorUID) continue;

        const std::optional<std::size_t> parent_actor_index = FindActorIndexByUID(parent_uid);
        if (!parent_actor_index.has_value()) continue;
        if (*parent_actor_index == actor_index) continue;

        parent_actor_indices_[actor_index] = *parent_actor_index;
        children_by_actor_index_[*parent_actor_index].emplace_back(actor_index);
    }

    hierarchy_cache_dirty_ = false;
}

/*
Rebuild the derived physics-hierarchy state for every actor by combining local
Rigidbody requests with the actor-tree ancestry built above.
*/
void SceneDocument::RebuildPhysicsHierarchyCache() const {
    if (!physics_hierarchy_cache_dirty_) return;

    RebuildHierarchyCache();

    const std::size_t actor_count = scene_asset_.actors.size();
    physics_hierarchy_states_by_actor_index_.assign(actor_count, {});
    std::vector<bool> resolved(actor_count, false);
    std::vector<bool> resolving(actor_count, false);

    std::function<void(std::size_t)> resolve_subtree =
        [&](std::size_t actor_index) {
            if (actor_index >= actor_count) return;
            if (resolved[actor_index]) return;
            if (resolving[actor_index]) return;
            resolving[actor_index] = true;

            /*
            First collect the actor's own Rigidbody request, then layer parent
            ownership rules on top of it. The authored component stays intact;
            only the derived physics-hierarchy state is overridden.
            */
            PhysicsHierarchy::State state = BuildSceneActorPhysicsSelfState(*this, actor_index);

            std::uint64_t nearest_dynamic_ancestor_uid = PhysicsHierarchy::kInvalidActorUID;
            const std::optional<std::size_t> parent_actor_index = FindParentActorIndex(actor_index);
            if (parent_actor_index.has_value() &&
                *parent_actor_index < actor_count) {
                /*
                Resolve parents first so each child can cheaply inherit the
                nearest dynamic ancestor and physics-root classification.
                */
                resolve_subtree(*parent_actor_index);
                const PhysicsHierarchy::State &parent_state = physics_hierarchy_states_by_actor_index_[*parent_actor_index];
                const ActorUID parent_uid = GetActorUID(*parent_actor_index);
                if (parent_state.has_dynamic_rigidbody_self) {
                    nearest_dynamic_ancestor_uid = parent_uid;
                } 
                else {
                    nearest_dynamic_ancestor_uid = parent_state.nearest_dynamic_body_ancestor_uid;
                }
            }

            state.nearest_dynamic_body_ancestor_uid = nearest_dynamic_ancestor_uid;
            state.is_under_dynamic_hierarchy = nearest_dynamic_ancestor_uid != PhysicsHierarchy::kInvalidActorUID;
            if (state.has_dynamic_rigidbody_self) {
                state.physics_root_uid = GetActorUID(actor_index);
            } 
            else if (state.is_under_dynamic_hierarchy) {
                state.physics_root_uid = nearest_dynamic_ancestor_uid;
            }
            if (state.rigidbody_enabled_self && state.requested_body_type == "dynamic" && state.is_under_dynamic_hierarchy) {
                state.effective_body_type = "kinematic";
            }

            physics_hierarchy_states_by_actor_index_[actor_index] = std::move(state);
            resolving[actor_index] = false;
            resolved[actor_index] = true;
        };

    for (std::size_t actor_index = 0; actor_index < actor_count; ++actor_index) {
        resolve_subtree(actor_index);
    }

    physics_hierarchy_cache_dirty_ = false;
}

void SceneDocument::MarkDirty() {
    dirty_ = true;
}
