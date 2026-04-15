#ifndef SCENE_DOCUMENT_H
#define SCENE_DOCUMENT_H

#include "scene/Actor.h"
#include "shared/physics/PhysicsHierarchyState.h"
#include "shared/scene_format/SceneFormat.h"
#include "shared/scene_format/SceneMutation.h"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class SceneDocument {
public:
    using ActorRecord = SceneFormat::ActorRecord;
    using ActorUID = SceneFormat::SceneActorUID;
    static inline constexpr ActorUID kInvalidActorUID = SceneFormat::kInvalidSceneActorUID;

    /*
    SceneDocument owns the editor-side cache for one .scene file. The editor
    mutates this cache first, then mirrors it into runtime when needed, and
    only writes it back to disk on explicit save / shutdown.
    Load one scene file into the editor-owned cache
    */
    bool LoadFromSceneName(const std::string &scene_name);
    // Load one scene file from an explicit .scene path under resources/scenes
    bool LoadFromScenePath(const std::filesystem::path &scene_path);
    // Persist the current cache back into the original scene file
    bool Save();

    // Return whether the editor cache diverged from disk contents.
    bool IsDirty() const;
    // Return the scene name that was loaded from game.config / disk.
    const std::string &GetSceneName() const;
    // Return the resolved .scene source path on disk.
    const std::filesystem::path &GetScenePath() const;
    // Return the scene subdirectory relative to resources/scenes.
    const std::filesystem::path &GetSceneSubdirectory() const;

    // Return immutable access to the raw actor cache.
    const std::vector<ActorRecord> &GetActorRecords() const;
    // Return the shared scene asset that backs this document cache.
    const SceneFormat::SceneAsset &GetSceneAsset() const;
    std::size_t GetActorCount() const;
    /*
    Return the stable scene actor identity used for session/runtime mapping and
    persisted parent links.
    */
    ActorUID GetActorUID(std::size_t actor_index) const;
    std::optional<std::size_t> FindActorIndexByUID(ActorUID actor_uid) const;
    std::optional<std::size_t> FindParentActorIndex(std::size_t actor_index) const;
    std::vector<std::size_t> GetChildActorIndices(std::size_t actor_index) const;
    std::vector<std::size_t> GetRootActorIndices() const;
    // Return cached read-only physics ancestry info for one actor.
    PhysicsHierarchy::State GetPhysicsHierarchyState(
        std::size_t actor_index) const;

    // Build a merged actor view that includes template inheritance.
    Actor BuildEffectiveActor(std::size_t actor_index) const;
    // Return the display name shown in the hierarchy.
    std::string GetActorDisplayName(std::size_t actor_index) const;
    bool TryGetActorLocalTransform(std::size_t actor_index,
                                   std::string *out_component_key,
                                   float &out_x, float &out_y,
                                   float &out_rotation) const;
    bool TryGetActorWorldTransform(std::size_t actor_index,
                                   std::string *out_component_key,
                                   float &out_x, float &out_y,
                                   float &out_rotation) const;
    
    /*
    Return editable properties for one component, including default scalar
    values contributed by the component type table.
    */
    std::vector<Actor::ComponentProperty> GetInspectableProperties(std::size_t actor_index, const std::string &component_key) const;

    /*
    Mutate actor/component data inside the editor cache. Successful edits
    mark the document dirty so save/reload logic can react once per frame.
    */
    bool ApplyMutation(const SceneFormat::SceneMutation &mutation);
    bool ApplyEditCommand(const SceneFormat::SceneEditCommand &command);
    bool SetActorName(std::size_t actor_index, const std::string &name, SceneFormat::SceneEditCommand *out_command = nullptr);
    bool SetActorParent(
        std::size_t actor_index,
        std::optional<std::size_t> parent_actor_index,
        SceneFormat::SceneEditCommand *out_command = nullptr);
    bool DeleteActor(std::size_t actor_index, SceneFormat::SceneEditCommand *out_command = nullptr);
    bool DuplicateActor(std::size_t actor_index, std::size_t &out_actor_index, SceneFormat::SceneEditCommand *out_command = nullptr);
    bool SetComponentType(std::size_t actor_index, const std::string &component_key,
                          const std::string &type_name,
                          SceneFormat::SceneEditCommand *out_command = nullptr);
    bool RenameComponent(std::size_t actor_index,
                         const std::string &component_key,
                         const std::string &new_component_key,
                         SceneFormat::SceneEditCommand *out_command = nullptr);
    bool DeleteComponent(std::size_t actor_index, const std::string &component_key,
                         SceneFormat::SceneEditCommand *out_command = nullptr);
    bool DuplicateComponent(std::size_t actor_index,
                            const std::string &component_key,
                            std::string &out_new_component_key,
                            SceneFormat::SceneEditCommand *out_command = nullptr);
    bool SetComponentProperty(std::size_t actor_index,
                              const std::string &component_key,
                              const std::string &property_name,
                              const Actor::ComponentPropertyValue &value,
                              SceneFormat::SceneEditCommand *out_command = nullptr);

    // Append a brand-new actor into the scene cache and return its index.
    bool AppendEmptyActor(std::size_t &out_actor_index, SceneFormat::SceneEditCommand *out_command = nullptr);
    // Append one actor that references an existing .template asset.
    bool AppendActorFromTemplate(const std::string &template_name,
                                 std::size_t &out_actor_index,
                                 SceneFormat::SceneEditCommand *out_command = nullptr);
    // Add one component spec override to an actor in scene cache.
    bool AddComponentToActor(std::size_t actor_index,
                             const std::string &component_type,
                             SceneFormat::SceneEditCommand *out_command = nullptr);
    bool HasRawComponent(std::size_t actor_index,  const std::string &component_key) const;
    bool IsTemplateBackedComponent(std::size_t actor_index, const std::string &component_key) const;

private:
    // Allocate one new-actor display name from this document-owned session
    // counter. Play-mode copies inherit the current value, but unsaved edits
    // never leak back into the persisted private state file.
    std::string AllocateNextNewActorName();
    void ResetNewActorCounterState();
    void LoadPersistedNewActorCounter();
    void PersistNewActorCounter();
    std::string BuildUniqueActorName(const std::string &base_name) const;
    std::string BuildUniqueComponentKey(std::size_t actor_index,
                                        const std::string &base_key) const;
    ActorRecord *FindActorRecord(std::size_t actor_index);
    const ActorRecord *FindActorRecord(std::size_t actor_index) const;
    Actor::ComponentSpec *FindRawComponentSpec(std::size_t actor_index, const std::string &component_key);
    Actor::ComponentSpec *FindOrCreateRawComponentSpec(std::size_t actor_index, const std::string &component_key, const std::string &fallback_type_name);
    void SyncPoseLinkedComponentProperty(
        std::size_t actor_index, const std::string &source_component_key,
        const std::string &property_name,
        const Actor::ComponentPropertyValue &value);
    
    // Apply one mutation to the editor cache
    bool ApplyCreateActorMutation(const SceneFormat::CreateActorMutation &mutation);
    bool ApplyDeleteActorMutation(const SceneFormat::DeleteActorMutation &mutation);
    bool ApplySetActorNameMutation(const SceneFormat::SetActorNameMutation &mutation);
    bool ApplySetActorParentMutation(
        const SceneFormat::SetActorParentMutation &mutation);
    bool ApplyAddComponentMutation(const SceneFormat::AddComponentMutation &mutation);
    bool ApplyDeleteComponentMutation(const SceneFormat::DeleteComponentMutation &mutation);
    bool ApplyRenameComponentMutation(const SceneFormat::RenameComponentMutation &mutation);
    bool ApplySetComponentTypeMutation(const SceneFormat::SetComponentTypeMutation &mutation);
    bool ApplySetComponentPropertyMutation(const SceneFormat::SetComponentPropertyMutation &mutation);

    void ReassignFreshActorUIDs();
    void SyncActorUIDsIntoSceneAsset();
    void InvalidateHierarchyCache();
    void InvalidatePhysicsHierarchyCache();
    void RebuildHierarchyCache() const;
    void RebuildPhysicsHierarchyCache() const;
    void MarkDirty();

    SceneFormat::SceneAsset scene_asset_;
    std::vector<ActorUID> actor_uids_;
    mutable bool hierarchy_cache_dirty_ = true;
    mutable std::vector<std::optional<std::size_t>> parent_actor_indices_;
    mutable std::vector<std::vector<std::size_t>> children_by_actor_index_;
    mutable bool physics_hierarchy_cache_dirty_ = true;
    mutable std::vector<PhysicsHierarchy::State>
        physics_hierarchy_states_by_actor_index_;
    bool dirty_ = false;
    int last_allocated_new_actor_ordinal_ = 0;
    bool new_actor_counter_dirty_ = false;
};

#endif
