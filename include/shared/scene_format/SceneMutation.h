#ifndef SHARED_SCENE_FORMAT_SCENE_MUTATION_H
#define SHARED_SCENE_FORMAT_SCENE_MUTATION_H

#include "shared/scene_format/SceneFormat.h"
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace SceneFormat {

using SceneActorUID = std::uint64_t;
inline constexpr SceneActorUID kInvalidSceneActorUID = 0;

struct CreateActorMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
    std::optional<SceneActorUID> insert_after_actor_uid;
    ActorRecord actor_record;
};

struct DeleteActorMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
};

struct SetActorNameMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
    std::string actor_name;
};

struct SetActorParentMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
    std::optional<SceneActorUID> parent_uid;
};

struct AddComponentMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
    Actor::ComponentSpec component_spec;
};

struct DeleteComponentMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
    std::string component_key;
};

struct RenameComponentMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
    std::string component_key;
    std::string new_component_key;
};

struct SetComponentTypeMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
    std::string component_key;
    std::string type_name;
};

struct SetComponentPropertyMutation {
    SceneActorUID actor_uid = kInvalidSceneActorUID;
    std::string component_key;
    std::string property_name;
    Actor::ComponentPropertyValue value;
};

using SceneMutationPayload =
    std::variant<CreateActorMutation, DeleteActorMutation, SetActorNameMutation,
                 SetActorParentMutation, AddComponentMutation,
                 DeleteComponentMutation, RenameComponentMutation,
                 SetComponentTypeMutation, SetComponentPropertyMutation>;

struct SceneMutation {
    SceneMutationPayload payload;
};

struct SceneEditCommand {
    std::vector<SceneMutation> mutations;

    bool empty() const { return mutations.empty(); }

    static SceneEditCommand Single(SceneMutation mutation) {
        SceneEditCommand command;
        command.mutations.emplace_back(std::move(mutation));
        return command;
    }
};

} // namespace SceneFormat

#endif
