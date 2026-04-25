#include "editor/documents/SceneDocument.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "shared/resources/ResourcePath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <unordered_set>

namespace {

bool ArePropertyValuesEqual(const Actor::ComponentPropertyValue &a,
                            const Actor::ComponentPropertyValue &b) {
    return a == b;
}

bool IsStringBlank(const std::string &value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

std::string BuildIndexedName(const std::string &base_name, int index) {
    std::ostringstream name_builder;
    name_builder << base_name << " (" << index << ")";
    return name_builder.str();
}

void RotateClockwise(float x, float y, float rotation_degrees, float &out_x,
                     float &out_y) {
    const float radians =
        rotation_degrees * (3.14159265358979323846f / 180.0f);
    const float cos_theta = std::cos(radians);
    const float sin_theta = std::sin(radians);
    out_x = cos_theta * x + sin_theta * y;
    out_y = -sin_theta * x + cos_theta * y;
}

void ResolveReparentedLocalTransform(float actor_world_x, float actor_world_y,
                                     float actor_world_rotation,
                                     std::optional<float> parent_world_x,
                                     std::optional<float> parent_world_y,
                                     std::optional<float> parent_world_rotation,
                                     float &out_local_x, float &out_local_y,
                                     float &out_local_rotation) {
    if (!parent_world_x.has_value() || !parent_world_y.has_value() ||
        !parent_world_rotation.has_value()) {
        out_local_x = actor_world_x;
        out_local_y = actor_world_y;
        out_local_rotation = actor_world_rotation;
        return;
    }

    const float relative_world_x = actor_world_x - *parent_world_x;
    const float relative_world_y = actor_world_y - *parent_world_y;
    RotateClockwise(relative_world_x, relative_world_y, -*parent_world_rotation,
                    out_local_x, out_local_y);
    out_local_rotation = actor_world_rotation - *parent_world_rotation;
}

constexpr const char *kActorCounterTableKey = "new_actor_counter_by_scene";
constexpr const char *kLegacyEditorPrivateStatePath =
    ".engine/editor_private_state.json";

/*
Project-private editor state is stored in a hidden folder inside the logical
resources root. It moves with the project and is hidden from the Project panel.
*/
rapidjson::Document BuildDefaultEditorPrivateState() {
    rapidjson::Document document;
    document.SetObject();
    rapidjson::Value actor_counter_table(rapidjson::kObjectType);
    document.AddMember(
        rapidjson::Value(kActorCounterTableKey, document.GetAllocator()).Move(),
        actor_counter_table, document.GetAllocator());
    return document;
}

void EnsureEnginePrivateStorageExists() {
    ResourcePath::EnsureDirectoryExists(ResourcePath::ProjectHiddenRoot());

    const std::filesystem::path private_state_file =
        ResourcePath::ProjectEditorPrivateStatePath();
    if (std::filesystem::exists(private_state_file)) return;
    if (std::filesystem::exists(kLegacyEditorPrivateStatePath)) return;

    const rapidjson::Document default_state = BuildDefaultEditorPrivateState();
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    default_state.Accept(writer);

    std::ofstream output_file(private_state_file,
                              std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
        return;
    }
    output_file << buffer.GetString() << std::endl;
}

std::filesystem::path ResolveEditorPrivateStateReadPath() {
    const std::filesystem::path private_state_file =
        ResourcePath::ProjectEditorPrivateStatePath();
    if (std::filesystem::exists(private_state_file)) {
        return private_state_file;
    }
    if (std::filesystem::exists(kLegacyEditorPrivateStatePath)) {
        return kLegacyEditorPrivateStatePath;
    }
    return private_state_file;
}

rapidjson::Document ReadEditorPrivateState() {
    EnsureEnginePrivateStorageExists();

    const std::filesystem::path private_state_file =
        ResolveEditorPrivateStateReadPath();
    std::ifstream input_file(private_state_file, std::ios::in);
    if (!input_file.is_open()) {
        return BuildDefaultEditorPrivateState();
    }

    std::stringstream content_stream;
    content_stream << input_file.rdbuf();
    rapidjson::Document document;
    document.Parse(content_stream.str().c_str());
    if (!document.IsObject()) {
        return BuildDefaultEditorPrivateState();
    }

    if (!document.HasMember(kActorCounterTableKey) ||
        !document[kActorCounterTableKey].IsObject()) {
        rapidjson::Value actor_counter_table(rapidjson::kObjectType);
        if (document.HasMember(kActorCounterTableKey)) {
            document.RemoveMember(kActorCounterTableKey);
        }
        document.AddMember(
            rapidjson::Value(kActorCounterTableKey, document.GetAllocator())
                .Move(),
            actor_counter_table, document.GetAllocator());
    }

    return document;
}

void WriteEditorPrivateState(const rapidjson::Document &document) {
    EnsureEnginePrivateStorageExists();
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    std::ofstream output_file(ResourcePath::ProjectEditorPrivateStatePath(),
                              std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
        return;
    }
    output_file << buffer.GetString() << std::endl;
}

std::string BuildSceneCounterKey(const SceneFormat::SceneAsset &scene_asset) {
    if (!scene_asset.scene_path.empty()) {
        std::error_code relative_error;
        const std::filesystem::path relative_path = std::filesystem::relative(
            scene_asset.scene_path.lexically_normal(),
            ResourcePath::ResourcesRootPath().lexically_normal(),
            relative_error);
        if (!relative_error && !relative_path.empty()) {
            return relative_path.generic_string();
        }
        return scene_asset.scene_path.lexically_normal().generic_string();
    }
    if (!scene_asset.scene_name.empty()) {
        return scene_asset.scene_name;
    }
    return "__unspecified_scene__";
}

int ReadPersistedNewActorOrdinal(const SceneFormat::SceneAsset &scene_asset) {
    rapidjson::Document document = ReadEditorPrivateState();
    rapidjson::Value &counter_table = document[kActorCounterTableKey];
    const std::string scene_key = BuildSceneCounterKey(scene_asset);
    if (counter_table.HasMember(scene_key.c_str()) &&
        counter_table[scene_key.c_str()].IsInt()) {
        return std::max(0, counter_table[scene_key.c_str()].GetInt());
    }
    return 0;
}

void WritePersistedNewActorOrdinal(const SceneFormat::SceneAsset &scene_asset,
                                   int last_allocated_ordinal) {
    rapidjson::Document document = ReadEditorPrivateState();
    rapidjson::Value &counter_table = document[kActorCounterTableKey];

    const std::string scene_key = BuildSceneCounterKey(scene_asset);
    const int clamped_ordinal = std::max(0, last_allocated_ordinal);
    if (counter_table.HasMember(scene_key.c_str()) &&
        counter_table[scene_key.c_str()].IsInt()) {
        counter_table[scene_key.c_str()].SetInt(clamped_ordinal);
    } else {
        counter_table.AddMember(
            rapidjson::Value(scene_key.c_str(), document.GetAllocator()).Move(),
            rapidjson::Value(clamped_ordinal).Move(), document.GetAllocator());
    }

    WriteEditorPrivateState(document);
}

} // namespace

/* --------------------------------------------------------------------------
SceneDocument public API: editor mutations
-------------------------------------------------------------------------- */

/*
Apply one serialized scene mutation to the editor-owned cache. This is the same
command format later mirrored into runtime, which keeps authoring and play-mode
mutation logic aligned.
*/
bool SceneDocument::ApplyMutation(const SceneFormat::SceneMutation &mutation) {
    return std::visit(
        [&](const auto &typed_mutation) -> bool {
            using MutationType = std::decay_t<decltype(typed_mutation)>;
            if constexpr (std::is_same_v<MutationType,
                                         SceneFormat::CreateActorMutation>) {
                return ApplyCreateActorMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::DeleteActorMutation>) {
                return ApplyDeleteActorMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::SetActorNameMutation>) {
                return ApplySetActorNameMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::SetActorParentMutation>) {
                return ApplySetActorParentMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::AddComponentMutation>) {
                return ApplyAddComponentMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::DeleteComponentMutation>) {
                return ApplyDeleteComponentMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::RenameComponentMutation>) {
                return ApplyRenameComponentMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::SetComponentTypeMutation>) {
                return ApplySetComponentTypeMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::SetComponentPropertyMutation>) {
                return ApplySetComponentPropertyMutation(typed_mutation);
            }
            return false;
        },
        mutation.payload);
}

/* Apply one batch edit command atomically to the in-memory scene document. */
bool SceneDocument::ApplyEditCommand(const SceneFormat::SceneEditCommand &command) {
    if (command.empty()) return false;

    for (const SceneFormat::SceneMutation &mutation : command.mutations) {
        if (!ApplyMutation(mutation)) {
            return false;
        }
    }
    return true;
}

/* Rename one actor through the scene-mutation path. */
bool SceneDocument::SetActorName(std::size_t actor_index,
                                 const std::string &name,
                                 SceneFormat::SceneEditCommand *out_command) {
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    SceneFormat::SetActorNameMutation mutation;
    mutation.actor_uid = actor_uid;
    mutation.actor_name = name;
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/*
Request a reparent operation in scene space. The resulting mutation preserves
world transform, then rewrites local Transform data so the actor does not jump
when moved under a new parent.
*/
bool SceneDocument::SetActorParent(
    std::size_t actor_index, std::optional<std::size_t> parent_actor_index,
    SceneFormat::SceneEditCommand *out_command) {
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    if (parent_actor_index.has_value()) {
        if (*parent_actor_index >= scene_asset_.actors.size()) return false;
        if (*parent_actor_index == actor_index) return false;
    }

    SceneFormat::SetActorParentMutation mutation;
    mutation.actor_uid = actor_uid;
    if (parent_actor_index.has_value()) {
        const ActorUID parent_uid = GetActorUID(*parent_actor_index);
        if (parent_uid == kInvalidActorUID) return false;
        mutation.parent_uid = parent_uid;
    }

    const SceneFormat::SceneEditCommand command = SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/* Delete one actor through the same mutation pipeline used by runtime mirroring. */
bool SceneDocument::DeleteActor(std::size_t actor_index,
                                SceneFormat::SceneEditCommand *out_command) {
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    SceneFormat::DeleteActorMutation mutation;
    mutation.actor_uid = actor_uid;
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/*
Duplicate one actor record, assign a fresh scene-backed UID, and insert it near
the source actor so hierarchy ordering stays intuitive for the editor user.
*/
bool SceneDocument::DuplicateActor(std::size_t actor_index,
                                   std::size_t &out_actor_index,
                                   SceneFormat::SceneEditCommand *out_command) {
    const ActorRecord *source_record = FindActorRecord(actor_index);
    const ActorUID source_actor_uid = GetActorUID(actor_index);
    if (source_record == nullptr || source_actor_uid == kInvalidActorUID) {
        return false;
    }

    ActorRecord duplicated_record = *source_record;
    duplicated_record.name =
        BuildUniqueActorName(GetActorDisplayName(actor_index) + " (copy)");
    std::unordered_set<ActorUID> used_uids(actor_uids_.begin(),
                                           actor_uids_.end());

    SceneFormat::CreateActorMutation mutation;
    mutation.actor_uid = AllocateNextSceneBackedActorUID(used_uids);
    mutation.insert_after_actor_uid = source_actor_uid;
    mutation.actor_record = std::move(duplicated_record);

    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;

    const std::optional<std::size_t> inserted_actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!inserted_actor_index.has_value()) return false;
    out_actor_index = *inserted_actor_index;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/* Change one component's type through the mutation path. */
bool SceneDocument::SetComponentType(
    std::size_t actor_index, const std::string &component_key,
    const std::string &type_name, SceneFormat::SceneEditCommand *out_command) {
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    SceneFormat::SetComponentTypeMutation mutation;
    mutation.actor_uid = actor_uid;
    mutation.component_key = component_key;
    mutation.type_name = type_name;
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/* Rename one component key through the mutation path. */
bool SceneDocument::RenameComponent(
    std::size_t actor_index, const std::string &component_key,
    const std::string &new_component_key,
    SceneFormat::SceneEditCommand *out_command) {
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    SceneFormat::RenameComponentMutation mutation;
    mutation.actor_uid = actor_uid;
    mutation.component_key = component_key;
    mutation.new_component_key = new_component_key;
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/* Delete one component through the mutation path. */
bool SceneDocument::DeleteComponent(
    std::size_t actor_index, const std::string &component_key,
    SceneFormat::SceneEditCommand *out_command) {
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    SceneFormat::DeleteComponentMutation mutation;
    mutation.actor_uid = actor_uid;
    mutation.component_key = component_key;
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/* Duplicate one effective component into a new raw component override. */
bool SceneDocument::DuplicateComponent(
    std::size_t actor_index, const std::string &component_key,
    std::string &out_new_component_key,
    SceneFormat::SceneEditCommand *out_command) {
    if (component_key.empty()) return false;
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    const Actor effective_actor = BuildEffectiveActor(actor_index);
    const Actor::ComponentSpec *source_component_spec =
        SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       component_key);
    if (source_component_spec == nullptr) return false;

    Actor::ComponentSpec duplicated_component = *source_component_spec;
    duplicated_component.key = BuildUniqueComponentKey(actor_index,
                                                       "new component");
    if (duplicated_component.key.empty()) return false;

    SceneFormat::AddComponentMutation mutation;
    mutation.actor_uid = actor_uid;
    mutation.component_spec = duplicated_component;
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;

    out_new_component_key = duplicated_component.key;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/* Set one editable scalar component property through the mutation path. */
bool SceneDocument::SetComponentProperty(
    std::size_t actor_index, const std::string &component_key,
    const std::string &property_name,
    const Actor::ComponentPropertyValue &value,
    SceneFormat::SceneEditCommand *out_command) {
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    SceneFormat::SetComponentPropertyMutation mutation;
    mutation.actor_uid = actor_uid;
    mutation.component_key = component_key;
    mutation.property_name = property_name;
    mutation.value = value;
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/*
Append a brand-new actor into the scene cache and return its index. Empty
actors are always seeded with a built-in Transform so scene editing works
immediately after creation.
*/
bool SceneDocument::AppendEmptyActor(
    std::size_t &out_actor_index,
    SceneFormat::SceneEditCommand *out_command) {
    ActorRecord actor_record;
    actor_record.name = AllocateNextNewActorName();
    SceneFormat::EnsureBuiltinTransformComponent(actor_record.component_specs);
    std::unordered_set<ActorUID> used_uids(actor_uids_.begin(),
                                           actor_uids_.end());

    SceneFormat::CreateActorMutation mutation;
    mutation.actor_uid = AllocateNextSceneBackedActorUID(used_uids);
    mutation.actor_record = std::move(actor_record);
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;

    const std::optional<std::size_t> inserted_actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!inserted_actor_index.has_value()) return false;
    out_actor_index = *inserted_actor_index;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/*
Append one actor that references an existing .template asset. If the template
predates built-in Transform, we persist a scene-local Transform override so the
new actor still participates in scene parenting and dragging.
*/
bool SceneDocument::AppendActorFromTemplate(
    const std::string &template_name, std::size_t &out_actor_index,
    SceneFormat::SceneEditCommand *out_command) {
    if (template_name.empty()) return false;

    const SceneFormat::ActorTemplateAsset template_asset =
        SceneFormat::LoadActorTemplateAsset(template_name,
                                            scene_asset_.scene_subdirectory);
    if (template_asset.template_path.empty()) {
        return false;
    }

    ActorRecord actor_record;
    actor_record.template_name = template_name;
    actor_record.name = AllocateNextNewActorName();
    if (!SceneFormat::HasComponentType(template_asset.actor.component_specs,
                                       "Transform")) {
        SceneFormat::EnsureBuiltinTransformComponent(actor_record.component_specs);
    }
    std::unordered_set<ActorUID> used_uids(actor_uids_.begin(),
                                           actor_uids_.end());

    SceneFormat::CreateActorMutation mutation;
    mutation.actor_uid = AllocateNextSceneBackedActorUID(used_uids);
    mutation.actor_record = std::move(actor_record);
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;

    const std::optional<std::size_t> inserted_actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!inserted_actor_index.has_value()) return false;
    out_actor_index = *inserted_actor_index;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/* Add one component spec override to an actor in scene cache. */
bool SceneDocument::AddComponentToActor(
    std::size_t actor_index, const std::string &component_type,
    SceneFormat::SceneEditCommand *out_command) {
    if (component_type.empty()) return false;
    const ActorUID actor_uid = GetActorUID(actor_index);
    if (actor_uid == kInvalidActorUID) return false;

    Actor::ComponentSpec component_spec;
    component_spec.key = BuildUniqueComponentKey(actor_index, "new component");
    if (component_spec.key.empty()) return false;
    component_spec.type = component_type;

    SceneFormat::AddComponentMutation mutation;
    mutation.actor_uid = actor_uid;
    mutation.component_spec = std::move(component_spec);
    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

/* Report whether one component key exists directly in the raw scene cache. */
bool SceneDocument::HasRawComponent(std::size_t actor_index,
                                    const std::string &component_key) const {
    const ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) return false;
    return SceneFormat::FindComponentSpec(actor_record->component_specs,
                                          component_key) != nullptr;
}

/* Report whether one effective component originates from the referenced template. */
bool SceneDocument::IsTemplateBackedComponent(
    std::size_t actor_index, const std::string &component_key) const {
    const ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) return false;
    if (actor_record->template_name.empty()) return false;

    const SceneFormat::ActorTemplateAsset template_asset =
        SceneFormat::LoadActorTemplateAsset(actor_record->template_name,
                                            scene_asset_.scene_subdirectory);
    if (template_asset.template_path.empty()) return false;

    return SceneFormat::FindComponentSpec(template_asset.actor.component_specs,
                                          component_key) != nullptr;
}

/*
Apply actor creation directly to the document cache. This is the primitive used
by Add Actor, template drops, duplication, and runtime scene-command mirroring.
*/
bool SceneDocument::ApplyCreateActorMutation(
    const SceneFormat::CreateActorMutation &mutation) {
    if (mutation.actor_uid == kInvalidActorUID) return false;
    if (FindActorIndexByUID(mutation.actor_uid).has_value()) return false;

    ActorRecord actor_record = mutation.actor_record;
    actor_record.uid = mutation.actor_uid;

    std::size_t insert_index = scene_asset_.actors.size();
    if (mutation.insert_after_actor_uid.has_value()) {
        const std::optional<std::size_t> anchor_actor_index =
            FindActorIndexByUID(*mutation.insert_after_actor_uid);
        if (!anchor_actor_index.has_value()) return false;
        insert_index = *anchor_actor_index + 1;
    }

    const auto insert_it =
        scene_asset_.actors.begin() + static_cast<std::ptrdiff_t>(insert_index);
    scene_asset_.actors.insert(insert_it, std::move(actor_record));
    actor_uids_.insert(
        actor_uids_.begin() + static_cast<std::ptrdiff_t>(insert_index),
        mutation.actor_uid);
    SyncActorUIDsIntoSceneAsset();
    InvalidateHierarchyCache();
    MarkDirty();
    return true;
}

/*
Apply actor deletion and detach any children back to root. The scene format
persists only parent links, so there is no separate child list to rewrite here.
*/
bool SceneDocument::ApplyDeleteActorMutation(
    const SceneFormat::DeleteActorMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;

    scene_asset_.actors.erase(scene_asset_.actors.begin() +
                              static_cast<std::ptrdiff_t>(*actor_index));
    actor_uids_.erase(actor_uids_.begin() +
                      static_cast<std::ptrdiff_t>(*actor_index));
    for (ActorRecord &actor_record : scene_asset_.actors) {
        if (actor_record.parent_uid == mutation.actor_uid) {
            actor_record.parent_uid = kInvalidActorUID;
        }
    }
    SyncActorUIDsIntoSceneAsset();
    InvalidateHierarchyCache();
    MarkDirty();
    return true;
}

/* Apply an actor rename to the document cache. */
bool SceneDocument::ApplySetActorNameMutation(
    const SceneFormat::SetActorNameMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;
    ActorRecord *actor_record = FindActorRecord(*actor_index);
    if (actor_record == nullptr) return false;
    if (mutation.actor_name.empty() || IsStringBlank(mutation.actor_name)) {
        return false;
    }
    if (actor_record->name == mutation.actor_name) {
        return false;
    }

    actor_record->name = mutation.actor_name;
    MarkDirty();
    return true;
}

/*
Apply a reparent mutation inside the document cache. The actor keeps its world
pose, and we rewrite local Transform overrides afterward to preserve that pose
under the new parent chain.
*/
bool SceneDocument::ApplySetActorParentMutation(
    const SceneFormat::SetActorParentMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;

    const std::optional<std::size_t> current_parent_index =
        FindParentActorIndex(*actor_index);
    const std::optional<std::size_t> new_parent_index =
        mutation.parent_uid.has_value()
            ? FindActorIndexByUID(*mutation.parent_uid)
            : std::nullopt;
    if (mutation.parent_uid.has_value() && !new_parent_index.has_value()) {
        return false;
    }

    if (new_parent_index.has_value()) {
        if (*new_parent_index == *actor_index) return false;

        /*
        Reparent must not introduce a cycle. We walk the prospective parent's
        ancestor chain instead of children because parent links are the only
        relationship persisted in scene data.
        */
        std::unordered_set<std::size_t> visited_actor_indices;
        std::optional<std::size_t> ancestor_index = new_parent_index;
        while (ancestor_index.has_value()) {
            if (*ancestor_index == *actor_index) return false;
            if (!visited_actor_indices.insert(*ancestor_index).second) {
                return false;
            }
            ancestor_index = FindParentActorIndex(*ancestor_index);
        }
    }

    if (current_parent_index == new_parent_index) return false;

    float actor_world_x = 0.0f;
    float actor_world_y = 0.0f;
    float actor_world_rotation = 0.0f;
    std::string transform_component_key;
    const bool has_transform = TryGetActorWorldTransform(
        *actor_index, &transform_component_key, actor_world_x, actor_world_y,
        actor_world_rotation);

    std::optional<float> parent_world_x;
    std::optional<float> parent_world_y;
    std::optional<float> parent_world_rotation;
    if (has_transform && new_parent_index.has_value()) {
        float resolved_parent_world_x = 0.0f;
        float resolved_parent_world_y = 0.0f;
        float resolved_parent_world_rotation = 0.0f;
        if (TryGetActorWorldTransform(*new_parent_index, nullptr,
                                      resolved_parent_world_x,
                                      resolved_parent_world_y,
                                      resolved_parent_world_rotation)) {
            parent_world_x = resolved_parent_world_x;
            parent_world_y = resolved_parent_world_y;
            parent_world_rotation = resolved_parent_world_rotation;
        }
    }

    ActorRecord *actor_record = FindActorRecord(*actor_index);
    if (actor_record == nullptr) return false;
    actor_record->parent_uid = mutation.parent_uid.value_or(kInvalidActorUID);

    if (has_transform) {
        /*
        Parenting changes the frame of reference. We keep the actor visually in
        place by converting its previous world pose back into a local pose under
        the new parent before writing the mutation result into Transform.
        */
        float local_x = 0.0f;
        float local_y = 0.0f;
        float local_rotation = 0.0f;
        ResolveReparentedLocalTransform(
            actor_world_x, actor_world_y, actor_world_rotation, parent_world_x,
            parent_world_y, parent_world_rotation, local_x, local_y,
            local_rotation);

        Actor::ComponentSpec *raw_transform_component =FindOrCreateRawComponentSpec(*actor_index, transform_component_key, "Transform");
        if (raw_transform_component != nullptr) {
            SceneFormat::UpsertComponentProperty(*raw_transform_component, "x", static_cast<double>(local_x));
            SceneFormat::UpsertComponentProperty(*raw_transform_component, "y", static_cast<double>(local_y));
            SceneFormat::UpsertComponentProperty(*raw_transform_component, "rotation", static_cast<double>(local_rotation));
            SyncPoseLinkedComponentProperty(*actor_index, transform_component_key, "x", static_cast<double>(local_x));
            SyncPoseLinkedComponentProperty(*actor_index, transform_component_key, "y", static_cast<double>(local_y));
            SyncPoseLinkedComponentProperty(*actor_index, transform_component_key, "rotation", static_cast<double>(local_rotation));
        }
    }

    InvalidateHierarchyCache();
    MarkDirty();
    return true;
}

/* Add one raw component spec override to the document cache. */
bool SceneDocument::ApplyAddComponentMutation(
    const SceneFormat::AddComponentMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;
    if (mutation.component_spec.key.empty() ||
        mutation.component_spec.type.empty()) {
        return false;
    }
    if (mutation.component_spec.type == SceneFormat::kDeletedComponentType) {
        return false;
    }

    ActorRecord *actor_record = FindActorRecord(*actor_index);
    if (actor_record == nullptr) return false;

    const Actor effective_actor = BuildEffectiveActor(*actor_index);
    if (SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       mutation.component_spec.key) != nullptr) {
        return false;
    }

    Actor::ComponentSpec component_spec = mutation.component_spec;
    actor_record->component_specs.emplace_back(std::move(component_spec));
    SceneFormat::SortComponentSpecs(actor_record->component_specs);
    InvalidatePhysicsHierarchyCache();
    MarkDirty();
    return true;
}

/* Delete one component, including template-backed tombstone handling. */
bool SceneDocument::ApplyDeleteComponentMutation(
    const SceneFormat::DeleteComponentMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;
    if (mutation.component_key.empty()) return false;

    const Actor effective_actor = BuildEffectiveActor(*actor_index);
    if (SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       mutation.component_key) == nullptr) {
        return false;
    }

    ActorRecord *actor_record = FindActorRecord(*actor_index);
    if (actor_record == nullptr) return false;
    const bool template_backed =
        IsTemplateBackedComponent(*actor_index, mutation.component_key);

    Actor::ComponentSpec *raw_component =
        SceneFormat::FindComponentSpec(actor_record->component_specs,
                                       mutation.component_key);
    if (!template_backed) {
        if (raw_component == nullptr) return false;
        /*
        Pure scene-owned components can be erased outright because the document
        itself owns the whole component lifetime.
        */
        actor_record->component_specs.erase(
            std::remove_if(actor_record->component_specs.begin(), actor_record->component_specs.end(),
                           [&](const Actor::ComponentSpec &component_spec) {
                               return component_spec.key ==mutation.component_key;
                           }),
            actor_record->component_specs.end());
        InvalidatePhysicsHierarchyCache();
        MarkDirty();
        return true;
    }

    if (raw_component == nullptr) {
        /*
        Template-backed components cannot simply disappear from the effective
        actor view; we need a tombstone override so template inheritance knows
        this instance explicitly deleted the inherited component.
        */
        raw_component = FindOrCreateRawComponentSpec(*actor_index, mutation.component_key, SceneFormat::kDeletedComponentType);
        if (raw_component == nullptr) return false;
    }
    raw_component->type = SceneFormat::kDeletedComponentType;
    raw_component->overrides.clear();
    InvalidatePhysicsHierarchyCache();
    MarkDirty();
    return true;
}

/* Rename one component while respecting template-backed replacement rules. */
bool SceneDocument::ApplyRenameComponentMutation(
    const SceneFormat::RenameComponentMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;
    if (mutation.component_key.empty() || mutation.new_component_key.empty()) {
        return false;
    }
    if (IsStringBlank(mutation.new_component_key)) return false;
    if (mutation.component_key == mutation.new_component_key) return false;

    const Actor effective_actor = BuildEffectiveActor(*actor_index);
    const Actor::ComponentSpec *source_component_spec =
        SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       mutation.component_key);
    if (source_component_spec == nullptr) return false;
    if (SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       mutation.new_component_key) != nullptr) {
        return false;
    }

    const bool template_backed = IsTemplateBackedComponent(*actor_index, mutation.component_key);
    Actor::ComponentSpec *raw_source_component = FindRawComponentSpec(*actor_index, mutation.component_key);
    if (raw_source_component != nullptr && !template_backed) {
        /*
        If the component already exists as a raw scene-owned entry, renaming is
        just a key update plus re-sort of the actor's component list.
        */
        raw_source_component->key = mutation.new_component_key;
        ActorRecord *actor_record = FindActorRecord(*actor_index);
        if (actor_record == nullptr) return false;
        SceneFormat::SortComponentSpecs(actor_record->component_specs);
        InvalidatePhysicsHierarchyCache();
        MarkDirty();
        return true;
    }

    Actor::ComponentSpec *tombstone_component = FindOrCreateRawComponentSpec(
        *actor_index, mutation.component_key,
        SceneFormat::kDeletedComponentType);
    if (tombstone_component == nullptr) return false;
    /*
    Renaming a template-backed component is really "delete inherited key, then
    materialize a replacement under a new key" so the template stays untouched.
    */
    tombstone_component->type = SceneFormat::kDeletedComponentType;
    tombstone_component->overrides.clear();

    Actor::ComponentSpec replacement_component = *source_component_spec;
    replacement_component.key = mutation.new_component_key;
    ActorRecord *actor_record = FindActorRecord(*actor_index);
    if (actor_record == nullptr) return false;
    actor_record->component_specs.emplace_back(std::move(replacement_component));
    SceneFormat::SortComponentSpecs(actor_record->component_specs);
    InvalidatePhysicsHierarchyCache();
    MarkDirty();
    return true;
}

/* Change one component's type in the raw document cache. */
bool SceneDocument::ApplySetComponentTypeMutation(
    const SceneFormat::SetComponentTypeMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;
    if (mutation.type_name.empty()) return false;
    if (mutation.type_name == SceneFormat::kDeletedComponentType) return false;

    const Actor effective_actor = BuildEffectiveActor(*actor_index);
    const Actor::ComponentSpec *effective_component_spec =
        SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       mutation.component_key);
    Actor::ComponentSpec *raw_component_spec =
        FindRawComponentSpec(*actor_index, mutation.component_key);

    if (raw_component_spec == nullptr && effective_component_spec != nullptr &&
        effective_component_spec->type == mutation.type_name) {
        return false;
    }
    if (raw_component_spec != nullptr &&
        raw_component_spec->type == mutation.type_name) {
        return false;
    }

    raw_component_spec = FindOrCreateRawComponentSpec(*actor_index,
                                                      mutation.component_key,
                                                      mutation.type_name);
    if (raw_component_spec == nullptr) return false;
    raw_component_spec->type = mutation.type_name;
    InvalidatePhysicsHierarchyCache();
    MarkDirty();
    return true;
}

/*
Set one property override in the raw scene document. Pose-linked built-ins such
as Transform/Rigidbody are kept aligned here so authoring data stays coherent.
*/
bool SceneDocument::ApplySetComponentPropertyMutation(
    const SceneFormat::SetComponentPropertyMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;
    if (mutation.property_name.empty()) return false;

    const std::vector<Actor::ComponentProperty> current_properties =
        GetInspectableProperties(*actor_index, mutation.component_key);
    for (const Actor::ComponentProperty &property : current_properties) {
        if (property.name != mutation.property_name) continue;
        /*
        Skip no-op writes early so scene editing does not generate redundant
        mutation traffic or mark the document dirty unnecessarily.
        */
        if (ArePropertyValuesEqual(property.value, mutation.value)) {
            return false;
        }
        break;
    }

    Actor::ComponentSpec *raw_component_spec =
        FindOrCreateRawComponentSpec(*actor_index, mutation.component_key, "");
    if (raw_component_spec == nullptr) return false;

    SceneFormat::UpsertComponentProperty(*raw_component_spec,
                                         mutation.property_name,
                                         mutation.value);
    SyncPoseLinkedComponentProperty(*actor_index, mutation.component_key,
                                    mutation.property_name, mutation.value);
    InvalidatePhysicsHierarchyCache();
    MarkDirty();
    return true;
}

/* --------------------------------------------------------------------------
SceneDocument private helpers
-------------------------------------------------------------------------- */

void SceneDocument::SyncPoseLinkedComponentProperty(
    std::size_t actor_index, const std::string &source_component_key,
    const std::string &property_name,
    const Actor::ComponentPropertyValue &value) {
    if (property_name != "x" && property_name != "y" &&
        property_name != "rotation") {
        return;
    }

    /*
    Transform and Rigidbody share authored pose fields in this editor. When one
    side changes, we mirror the scalar override into the other so runtime and
    scene editing do not drift apart before play-mode sync runs.
    */
    const Actor effective_actor = BuildEffectiveActor(actor_index);
    const Actor::ComponentSpec *source_component_spec =
        SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       source_component_key);
    if (source_component_spec == nullptr) return;

    std::string linked_component_type;
    if (source_component_spec->type == "Transform") {
        linked_component_type = "Rigidbody";
    } else if (source_component_spec->type == "Rigidbody") {
        linked_component_type = "Transform";
    } else {
        return;
    }

    const Actor::ComponentSpec *linked_component_spec = nullptr;
    for (const Actor::ComponentSpec &component_spec :
         effective_actor.component_specs) {
        if (component_spec.type != linked_component_type) continue;
        linked_component_spec = &component_spec;
        break;
    }
    if (linked_component_spec == nullptr) return;

    Actor::ComponentSpec *raw_linked_component_spec =
        FindOrCreateRawComponentSpec(actor_index, linked_component_spec->key,
                                     linked_component_spec->type);
    if (raw_linked_component_spec == nullptr) return;

    SceneFormat::UpsertComponentProperty(*raw_linked_component_spec,
                                         property_name, value);
}

std::string SceneDocument::AllocateNextNewActorName() {
    ++last_allocated_new_actor_ordinal_;
    new_actor_counter_dirty_ = true;
    const int next_actor_ordinal = std::max(1, last_allocated_new_actor_ordinal_);
    const std::string candidate = BuildIndexedName("new actor", next_actor_ordinal);
    return BuildUniqueActorName(candidate);
}

void SceneDocument::ResetNewActorCounterState() {
    last_allocated_new_actor_ordinal_ = 0;
    new_actor_counter_dirty_ = false;
}

void SceneDocument::LoadPersistedNewActorCounter() {
    last_allocated_new_actor_ordinal_ = ReadPersistedNewActorOrdinal(scene_asset_);
    new_actor_counter_dirty_ = false;
}

void SceneDocument::PersistNewActorCounter() {
    if (scene_asset_.scene_path.empty()) return;
    if (!new_actor_counter_dirty_) return;
    WritePersistedNewActorOrdinal(scene_asset_, last_allocated_new_actor_ordinal_);
    new_actor_counter_dirty_ = false;
}

std::string SceneDocument::BuildUniqueActorName(
    const std::string &base_name) const {
    const std::string normalized_base =
        (base_name.empty() || IsStringBlank(base_name)) ? "new actor"
                                                        : base_name;

    std::unordered_set<std::string> used_names;
    used_names.reserve(scene_asset_.actors.size());
    for (std::size_t actor_index = 0; actor_index < scene_asset_.actors.size();
         ++actor_index) {
        used_names.emplace(GetActorDisplayName(actor_index));
    }

    if (used_names.find(normalized_base) == used_names.end()) {
        return normalized_base;
    }

    int suffix = 2;
    std::string candidate = BuildIndexedName(normalized_base, suffix);
    while (used_names.find(candidate) != used_names.end()) {
        ++suffix;
        candidate = BuildIndexedName(normalized_base, suffix);
    }
    return candidate;
}

std::string SceneDocument::BuildUniqueComponentKey(
    std::size_t actor_index, const std::string &base_key) const {
    const ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) return "";

    const std::string normalized_base =
        (base_key.empty() || IsStringBlank(base_key)) ? "new component"
                                                      : base_key;
    std::unordered_set<std::string> used_keys;
    used_keys.reserve(actor_record->component_specs.size() + 8);

    const Actor effective_actor = BuildEffectiveActor(actor_index);
    for (const Actor::ComponentSpec &component_spec :
         effective_actor.component_specs) {
        used_keys.emplace(component_spec.key);
    }
    for (const Actor::ComponentSpec &component_spec :
         actor_record->component_specs) {
        used_keys.emplace(component_spec.key);
    }

    int suffix = 1;
    std::string candidate = BuildIndexedName(normalized_base, suffix);
    while (used_keys.find(candidate) != used_keys.end()) {
        ++suffix;
        candidate = BuildIndexedName(normalized_base, suffix);
    }
    return candidate;
}

Actor::ComponentSpec *SceneDocument::FindOrCreateRawComponentSpec(
    std::size_t actor_index, const std::string &component_key,
    const std::string &fallback_type_name) {
    ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) return nullptr;

    Actor::ComponentSpec *existing_component_spec =
        SceneFormat::FindComponentSpec(actor_record->component_specs,
                                       component_key);
    if (existing_component_spec != nullptr) {
        if (existing_component_spec->type.empty() &&
            !fallback_type_name.empty()) {
            existing_component_spec->type = fallback_type_name;
        }
        return existing_component_spec;
    }

    Actor::ComponentSpec component_spec;
    component_spec.key = component_key;
    component_spec.type = fallback_type_name;
    actor_record->component_specs.emplace_back(std::move(component_spec));
    SceneFormat::SortComponentSpecs(actor_record->component_specs);
    return SceneFormat::FindComponentSpec(actor_record->component_specs,
                                          component_key);
}
