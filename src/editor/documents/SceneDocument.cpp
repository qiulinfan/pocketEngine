#include "editor/documents/SceneDocument.h"
#include "scene/Actor.h"
#include "shared/resources/ResourcePath.h"
#include "scripting/ComponentManager.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

bool ArePropertyValuesEqual(const Actor::ComponentPropertyValue &a,
                            const Actor::ComponentPropertyValue &b) {
    return a == b;
}

SceneDocument::ActorUID AllocateNextSceneDocumentActorUID(
    const std::unordered_set<SceneDocument::ActorUID> &used_uids) {
    static SceneDocument::ActorUID next_actor_uid = 1;
    constexpr SceneDocument::ActorUID kPreferredSceneActorUIDLimit =
        Actor::kRuntimeGeneratedEditorActorUIDStart;

    const auto find_available_uid =
        [&](SceneDocument::ActorUID begin_uid,
            SceneDocument::ActorUID end_uid_exclusive)
        -> SceneDocument::ActorUID {
        for (SceneDocument::ActorUID candidate_uid = begin_uid;
             candidate_uid < end_uid_exclusive; ++candidate_uid) {
            if (candidate_uid == SceneDocument::kInvalidActorUID) continue;
            if (used_uids.find(candidate_uid) != used_uids.end()) continue;
            return candidate_uid;
        }
        return SceneDocument::kInvalidActorUID;
    };

    if (next_actor_uid == SceneDocument::kInvalidActorUID ||
        next_actor_uid >= kPreferredSceneActorUIDLimit) {
        next_actor_uid = 1;
    }

    SceneDocument::ActorUID actor_uid =
        find_available_uid(next_actor_uid, kPreferredSceneActorUIDLimit);
    if (actor_uid == SceneDocument::kInvalidActorUID) {
        actor_uid = find_available_uid(1, kPreferredSceneActorUIDLimit);
    }
    if (actor_uid == SceneDocument::kInvalidActorUID) {
        actor_uid = kPreferredSceneActorUIDLimit;
        while (used_uids.find(actor_uid) != used_uids.end()) {
            ++actor_uid;
        }
    }

    next_actor_uid = actor_uid + 1;
    return actor_uid;
}

std::string BuildFallbackActorName(std::size_t actor_index) {
    std::ostringstream label;
    label << "Actor " << (actor_index + 1);
    return label.str();
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

bool TryReadPropertyAsString(
    const std::vector<Actor::ComponentProperty> &properties,
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

PhysicsHierarchy::State BuildSceneActorPhysicsSelfState(
    const SceneDocument &scene_document, std::size_t actor_index) {
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
        state.has_dynamic_rigidbody_self =
            enabled && requested_body_type == "dynamic";
        state.effective_body_type =
            enabled ? requested_body_type : PhysicsHierarchy::kNoBodyType;
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

constexpr const char *kEnginePrivateDir = ".engine";
constexpr const char *kEditorPrivateStatePath =
    ".engine/editor_private_state.json";
constexpr const char *kActorCounterTableKey = "new_actor_counter_by_scene";

rapidjson::Document BuildDefaultEditorPrivateState() {
    rapidjson::Document document;
    document.SetObject();
    rapidjson::Value actor_counter_table(rapidjson::kObjectType);
    document.AddMember(rapidjson::Value(kActorCounterTableKey,
                                        document.GetAllocator())
                           .Move(),
                       actor_counter_table, document.GetAllocator());
    return document;
}

void EnsureEnginePrivateStorageExists() {
    const std::filesystem::path private_directory(kEnginePrivateDir);
    if (!std::filesystem::exists(private_directory)) {
        std::filesystem::create_directories(private_directory);
    }

    const std::filesystem::path private_state_file(kEditorPrivateStatePath);
    if (std::filesystem::exists(private_state_file)) return;

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

rapidjson::Document ReadEditorPrivateState() {
    EnsureEnginePrivateStorageExists();

    const std::filesystem::path private_state_file(kEditorPrivateStatePath);
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
        document.AddMember(rapidjson::Value(kActorCounterTableKey,
                                            document.GetAllocator())
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

    std::ofstream output_file(kEditorPrivateStatePath,
                              std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
        return;
    }
    output_file << buffer.GetString() << std::endl;
}

std::string BuildSceneCounterKey(const SceneFormat::SceneAsset &scene_asset) {
    if (!scene_asset.scene_path.empty()) {
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

// -----------------------------------------------------------------------------
// SceneDocument public API: disk I/O
// -----------------------------------------------------------------------------

bool SceneDocument::LoadFromSceneName(const std::string &scene_name) {
    // This is the only place the editor document touches disk for loading.
    // After this point, all edits target the in-memory cache below.
    scene_asset_ = SceneFormat::LoadSceneAsset(scene_name);
    if (scene_asset_.scene_path.empty()) {
        actor_uids_.clear();
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
    // Convert absolute/relative file path into the same scene-name +
    // subdirectory pair used by runtime loading so both code paths stay
    // consistent.
    if (scene_path.empty()) {
        return false;
    }
    if (scene_path.extension() != ".scene") {
        return false;
    }

    const std::filesystem::path scene_root("resources/scenes");
    const std::filesystem::path normalized_scene_path =
        scene_path.lexically_normal();

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
    // Saving flushes the editor cache back into the original source .scene
    // file. Runtime stays a separate copy that will be refreshed explicitly.
    if (!SceneFormat::SaveSceneAsset(scene_asset_)) return false;
    PersistNewActorCounter();
    dirty_ = false;
    return true;
}

// -----------------------------------------------------------------------------
// SceneDocument public API: cache metadata / raw access
// -----------------------------------------------------------------------------

// Return whether the editor cache diverged from disk contents.
bool SceneDocument::IsDirty() const {
    return dirty_;
}

// Return the scene name that was loaded from game.config / disk.
const std::string &SceneDocument::GetSceneName() const {
    return scene_asset_.scene_name;
}

// Return the resolved .scene source path on disk.
const std::filesystem::path &SceneDocument::GetScenePath() const {
    return scene_asset_.scene_path;
}

// Return the scene subdirectory relative to resources/scenes.
const std::filesystem::path &SceneDocument::GetSceneSubdirectory() const {
    return scene_asset_.scene_subdirectory;
}

// Return immutable access to the raw actor cache.
const std::vector<SceneDocument::ActorRecord> &SceneDocument::GetActorRecords() const {
    return scene_asset_.actors;
}

// Return the shared scene asset that backs this document cache.
const SceneFormat::SceneAsset &SceneDocument::GetSceneAsset() const {
    return scene_asset_;
}

// Return the number of raw actor records currently stored in the cache.
std::size_t SceneDocument::GetActorCount() const {
    return scene_asset_.actors.size();
}

// Return the stable scene actor identity used for session/runtime mapping.
SceneDocument::ActorUID SceneDocument::GetActorUID(std::size_t actor_index) const {
    if (actor_index >= actor_uids_.size()) return kInvalidActorUID;
    return actor_uids_[actor_index];
}

// Find one cached actor index by its stable editor-only UID.
std::optional<std::size_t> SceneDocument::FindActorIndexByUID(
    ActorUID actor_uid) const {
    if (actor_uid == kInvalidActorUID) return std::nullopt;

    for (std::size_t actor_index = 0; actor_index < actor_uids_.size();
         ++actor_index) {
        if (actor_uids_[actor_index] == actor_uid) {
            return actor_index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> SceneDocument::FindParentActorIndex(
    std::size_t actor_index) const {
    RebuildHierarchyCache();
    if (actor_index >= parent_actor_indices_.size()) return std::nullopt;
    return parent_actor_indices_[actor_index];
}

std::vector<std::size_t> SceneDocument::GetChildActorIndices(
    std::size_t actor_index) const {
    RebuildHierarchyCache();
    if (actor_index >= children_by_actor_index_.size()) return {};
    return children_by_actor_index_[actor_index];
}

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

PhysicsHierarchy::State SceneDocument::GetPhysicsHierarchyState(
    std::size_t actor_index) const {
    RebuildPhysicsHierarchyCache();
    if (actor_index >= physics_hierarchy_states_by_actor_index_.size()) {
        return {};
    }
    return physics_hierarchy_states_by_actor_index_[actor_index];
}

// Mutate actor/component data inside the editor cache.
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

// Mutate actor/component data inside the editor cache.
bool SceneDocument::ApplyEditCommand(
    const SceneFormat::SceneEditCommand &command) {
    if (command.empty()) return false;

    for (const SceneFormat::SceneMutation &mutation : command.mutations) {
        if (!ApplyMutation(mutation)) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// SceneDocument public API: derived runtime/effective views
// -----------------------------------------------------------------------------

// Build a merged actor view that includes template inheritance.
Actor SceneDocument::BuildEffectiveActor(std::size_t actor_index) const {
    const ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) {
        return Actor();
    }
    return SceneFormat::BuildEffectiveActor(*actor_record,
                                            scene_asset_.scene_subdirectory);
}

// Return the display name shown in the hierarchy.
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

        const std::optional<std::size_t> parent_actor_index =
            FindParentActorIndex(current_actor_index);
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

// Return editable properties for one component, including default scalar
// values contributed by the component type table.
std::vector<Actor::ComponentProperty> SceneDocument::GetInspectableProperties(
    std::size_t actor_index, const std::string &component_key) const {
    // Inspector starts from the effective runtime-facing component view, then
    // overlays script-declared defaults so users can edit both inherited and
    // explicitly overridden scalar properties in one place.
    std::vector<Actor::ComponentProperty> properties;
    const Actor effective_actor = BuildEffectiveActor(actor_index);
    const Actor::ComponentSpec *component_spec =
        SceneFormat::FindComponentSpec(effective_actor.component_specs,
                                       component_key);
    if (component_spec == nullptr) return properties;

    properties =
        ComponentManager::GetComponentTypeDefaultProperties(component_spec->type);
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

// -----------------------------------------------------------------------------
// SceneDocument public API: editor mutations
// -----------------------------------------------------------------------------

// Mutate actor/component data inside the editor cache.
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
        mutation.parent_actor_uid = parent_uid;
    }

    const SceneFormat::SceneEditCommand command =
        SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

// Mutate actor/component data inside the editor cache.
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

// Mutate actor/component data inside the editor cache.
bool SceneDocument::DuplicateActor(std::size_t actor_index,
                                   std::size_t &out_actor_index,
                                   SceneFormat::SceneEditCommand *out_command) {
    const ActorRecord *source_record = FindActorRecord(actor_index);
    const ActorUID source_actor_uid = GetActorUID(actor_index);
    if (source_record == nullptr || source_actor_uid == kInvalidActorUID) {
        return false;
    }

    ActorRecord duplicated_record = *source_record;
    duplicated_record.name = BuildUniqueActorName(GetActorDisplayName(actor_index) + " (copy)");
    std::unordered_set<ActorUID> used_uids(actor_uids_.begin(), actor_uids_.end());

    SceneFormat::CreateActorMutation mutation;
    mutation.actor_uid = AllocateNextSceneDocumentActorUID(used_uids);
    mutation.insert_after_actor_uid = source_actor_uid;
    mutation.actor_record = std::move(duplicated_record);

    const SceneFormat::SceneEditCommand command = SceneFormat::SceneEditCommand::Single({mutation});
    if (!ApplyEditCommand(command)) return false;

    const std::optional<std::size_t> inserted_actor_index = FindActorIndexByUID(mutation.actor_uid);
    if (!inserted_actor_index.has_value()) return false;
    out_actor_index = *inserted_actor_index;
    if (out_command != nullptr) {
        *out_command = command;
    }
    return true;
}

// Mutate actor/component data inside the editor cache.
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

// Mutate actor/component data inside the editor cache.
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

// Mutate actor/component data inside the editor cache.
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

// Mutate actor/component data inside the editor cache.
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
    duplicated_component.key = BuildUniqueComponentKey(actor_index, "new component");
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

// Mutate actor/component data inside the editor cache.
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

// Append a brand-new actor into the scene cache and return its index.
bool SceneDocument::AppendEmptyActor(
    std::size_t &out_actor_index,
    SceneFormat::SceneEditCommand *out_command) {
    ActorRecord actor_record;
    actor_record.name = AllocateNextNewActorName();
    // Empty actors should always start with a local Transform so scene
    // parenting and viewport dragging work immediately.
    SceneFormat::EnsureBuiltinTransformComponent(actor_record.component_specs);
    std::unordered_set<ActorUID> used_uids(actor_uids_.begin(), actor_uids_.end());

    SceneFormat::CreateActorMutation mutation;
    mutation.actor_uid = AllocateNextSceneDocumentActorUID(used_uids);
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

// Append one actor that references an existing .template asset.
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
    // Older templates may not declare Transform explicitly yet. Persist one on
    // the scene instance so newly created actors still support parenting.
    if (!SceneFormat::HasComponentType(template_asset.actor.component_specs,
                                       "Transform")) {
        SceneFormat::EnsureBuiltinTransformComponent(
            actor_record.component_specs);
    }
    std::unordered_set<ActorUID> used_uids(actor_uids_.begin(), actor_uids_.end());

    SceneFormat::CreateActorMutation mutation;
    mutation.actor_uid = AllocateNextSceneDocumentActorUID(used_uids);
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

// Add one component spec override to an actor in scene cache.
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

// Report whether one component key exists directly in the raw scene cache.
bool SceneDocument::HasRawComponent(std::size_t actor_index,
                                    const std::string &component_key) const {
    const ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) return false;
    return SceneFormat::FindComponentSpec(actor_record->component_specs,
                                          component_key) != nullptr;
}

// Report whether one effective component originates from the referenced template.
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

bool SceneDocument::ApplyCreateActorMutation(
    const SceneFormat::CreateActorMutation &mutation) {
    if (mutation.actor_uid == kInvalidActorUID) return false;
    if (FindActorIndexByUID(mutation.actor_uid).has_value()) return false;

    ActorRecord actor_record = mutation.actor_record;
    actor_record.editor_actor_uid = mutation.actor_uid;

    std::size_t insert_index = scene_asset_.actors.size();
    if (mutation.insert_after_actor_uid.has_value()) {
        const std::optional<std::size_t> anchor_actor_index =
            FindActorIndexByUID(*mutation.insert_after_actor_uid);
        if (!anchor_actor_index.has_value()) return false;
        insert_index = *anchor_actor_index + 1;
    }

    const auto insert_it = scene_asset_.actors.begin() +
                           static_cast<std::ptrdiff_t>(insert_index);
    scene_asset_.actors.insert(insert_it, std::move(actor_record));
    actor_uids_.insert(actor_uids_.begin() + static_cast<std::ptrdiff_t>(insert_index),
                       mutation.actor_uid);
    SyncActorUIDsIntoSceneAsset();
    InvalidateHierarchyCache();
    MarkDirty();
    return true;
}

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
        if (actor_record.parent_actor_uid == mutation.actor_uid) {
            actor_record.parent_actor_uid = kInvalidActorUID;
        }
    }
    SyncActorUIDsIntoSceneAsset();
    InvalidateHierarchyCache();
    MarkDirty();
    return true;
}

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

bool SceneDocument::ApplySetActorParentMutation(
    const SceneFormat::SetActorParentMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;

    const std::optional<std::size_t> current_parent_index =
        FindParentActorIndex(*actor_index);
    const std::optional<std::size_t> new_parent_index =
        mutation.parent_actor_uid.has_value()
            ? FindActorIndexByUID(*mutation.parent_actor_uid)
            : std::nullopt;
    if (mutation.parent_actor_uid.has_value() && !new_parent_index.has_value()) {
        return false;
    }

    if (new_parent_index.has_value()) {
        if (*new_parent_index == *actor_index) return false;

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
    actor_record->parent_actor_uid =
        mutation.parent_actor_uid.value_or(kInvalidActorUID);

    if (has_transform) {
        float local_x = 0.0f;
        float local_y = 0.0f;
        float local_rotation = 0.0f;
        ResolveReparentedLocalTransform(
            actor_world_x, actor_world_y, actor_world_rotation, parent_world_x,
            parent_world_y, parent_world_rotation, local_x, local_y,
            local_rotation);

        Actor::ComponentSpec *raw_transform_component =
            FindOrCreateRawComponentSpec(*actor_index, transform_component_key,
                                         "Transform");
        if (raw_transform_component != nullptr) {
            SceneFormat::UpsertComponentProperty(*raw_transform_component, "x",
                                                 static_cast<double>(local_x));
            SceneFormat::UpsertComponentProperty(*raw_transform_component, "y",
                                                 static_cast<double>(local_y));
            SceneFormat::UpsertComponentProperty(
                *raw_transform_component, "rotation",
                static_cast<double>(local_rotation));
            SyncPoseLinkedComponentProperty(
                *actor_index, transform_component_key, "x",
                static_cast<double>(local_x));
            SyncPoseLinkedComponentProperty(
                *actor_index, transform_component_key, "y",
                static_cast<double>(local_y));
            SyncPoseLinkedComponentProperty(
                *actor_index, transform_component_key, "rotation",
                static_cast<double>(local_rotation));
        }
    }

    InvalidateHierarchyCache();
    MarkDirty();
    return true;
}

bool SceneDocument::ApplyAddComponentMutation(
    const SceneFormat::AddComponentMutation &mutation) {
    const std::optional<std::size_t> actor_index =
        FindActorIndexByUID(mutation.actor_uid);
    if (!actor_index.has_value()) return false;
    if (mutation.component_spec.key.empty() || mutation.component_spec.type.empty()) {
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
        actor_record->component_specs.erase(
            std::remove_if(actor_record->component_specs.begin(),
                           actor_record->component_specs.end(),
                           [&](const Actor::ComponentSpec &component_spec) {
                               return component_spec.key ==
                                      mutation.component_key;
                           }),
            actor_record->component_specs.end());
        InvalidatePhysicsHierarchyCache();
        MarkDirty();
        return true;
    }

    if (raw_component == nullptr) {
        raw_component = FindOrCreateRawComponentSpec(
            *actor_index, mutation.component_key,
            SceneFormat::kDeletedComponentType);
        if (raw_component == nullptr) return false;
    }
    raw_component->type = SceneFormat::kDeletedComponentType;
    raw_component->overrides.clear();
    InvalidatePhysicsHierarchyCache();
    MarkDirty();
    return true;
}

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

    const bool template_backed =
        IsTemplateBackedComponent(*actor_index, mutation.component_key);
    Actor::ComponentSpec *raw_source_component =
        FindRawComponentSpec(*actor_index, mutation.component_key);
    if (raw_source_component != nullptr && !template_backed) {
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

// -----------------------------------------------------------------------------
// SceneDocument private helpers
// -----------------------------------------------------------------------------

SceneDocument::ActorRecord *SceneDocument::FindActorRecord(
    std::size_t actor_index) {
    if (actor_index >= scene_asset_.actors.size()) return nullptr;
    return &scene_asset_.actors[actor_index];
}

const SceneDocument::ActorRecord *SceneDocument::FindActorRecord(
    std::size_t actor_index) const {
    if (actor_index >= scene_asset_.actors.size()) return nullptr;
    return &scene_asset_.actors[actor_index];
}

Actor::ComponentSpec *SceneDocument::FindRawComponentSpec(
    std::size_t actor_index, const std::string &component_key) {
    ActorRecord *actor_record = FindActorRecord(actor_index);
    if (actor_record == nullptr) return nullptr;
    return SceneFormat::FindComponentSpec(actor_record->component_specs,
                                          component_key);
}

void SceneDocument::SyncPoseLinkedComponentProperty(
    std::size_t actor_index, const std::string &source_component_key,
    const std::string &property_name,
    const Actor::ComponentPropertyValue &value) {
    if (property_name != "x" && property_name != "y" &&
        property_name != "rotation") {
        return;
    }

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
        (base_name.empty() || IsStringBlank(base_name)) ? "new actor" : base_name;

    std::unordered_set<std::string> used_names;
    used_names.reserve(scene_asset_.actors.size());
    for (std::size_t actor_index = 0; actor_index < scene_asset_.actors.size();
         ++actor_index) {
        used_names.emplace(GetActorDisplayName(actor_index));
    }

    if (used_names.find(normalized_base) == used_names.end()) return normalized_base;

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
    for (const Actor::ComponentSpec &component_spec : effective_actor.component_specs) {
        used_keys.emplace(component_spec.key);
    }
    for (const Actor::ComponentSpec &component_spec : actor_record->component_specs) {
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
        if (existing_component_spec->type.empty() && !fallback_type_name.empty()) {
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

void SceneDocument::ReassignFreshActorUIDs() {
    actor_uids_.clear();
    actor_uids_.reserve(scene_asset_.actors.size());
    std::unordered_set<ActorUID> used_uids;
    used_uids.reserve(scene_asset_.actors.size());
    for (std::size_t actor_index = 0; actor_index < scene_asset_.actors.size();
         ++actor_index) {
        ActorUID actor_uid = scene_asset_.actors[actor_index].editor_actor_uid;
        if (actor_uid == kInvalidActorUID ||
            used_uids.find(actor_uid) != used_uids.end()) {
            actor_uid = AllocateNextSceneDocumentActorUID(used_uids);
        }
        actor_uids_.emplace_back(actor_uid);
        used_uids.insert(actor_uid);
    }
    SyncActorUIDsIntoSceneAsset();
}

void SceneDocument::SyncActorUIDsIntoSceneAsset() {
    const std::size_t actor_count = scene_asset_.actors.size();
    if (actor_uids_.size() < actor_count) {
        actor_uids_.resize(actor_count, kInvalidActorUID);
    } else if (actor_uids_.size() > actor_count) {
        actor_uids_.resize(actor_count);
    }

    for (std::size_t actor_index = 0; actor_index < actor_count; ++actor_index) {
        scene_asset_.actors[actor_index].editor_actor_uid = actor_uids_[actor_index];
    }
}

void SceneDocument::InvalidateHierarchyCache() {
    hierarchy_cache_dirty_ = true;
    InvalidatePhysicsHierarchyCache();
}

void SceneDocument::InvalidatePhysicsHierarchyCache() {
    physics_hierarchy_cache_dirty_ = true;
}

void SceneDocument::RebuildHierarchyCache() const {
    if (!hierarchy_cache_dirty_) return;

    const std::size_t actor_count = scene_asset_.actors.size();
    parent_actor_indices_.assign(actor_count, std::nullopt);
    children_by_actor_index_.assign(actor_count, {});

    for (std::size_t actor_index = 0; actor_index < actor_count; ++actor_index) {
        const ActorUID parent_uid = scene_asset_.actors[actor_index].parent_actor_uid;
        if (parent_uid == kInvalidActorUID) continue;

        const std::optional<std::size_t> parent_actor_index =
            FindActorIndexByUID(parent_uid);
        if (!parent_actor_index.has_value()) continue;
        if (*parent_actor_index == actor_index) continue;

        parent_actor_indices_[actor_index] = *parent_actor_index;
        children_by_actor_index_[*parent_actor_index].emplace_back(actor_index);
    }

    hierarchy_cache_dirty_ = false;
}

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

            PhysicsHierarchy::State state =
                BuildSceneActorPhysicsSelfState(*this, actor_index);

            std::uint64_t nearest_dynamic_ancestor_uid =
                PhysicsHierarchy::kInvalidActorUID;
            const std::optional<std::size_t> parent_actor_index =
                FindParentActorIndex(actor_index);
            if (parent_actor_index.has_value() &&
                *parent_actor_index < actor_count) {
                resolve_subtree(*parent_actor_index);
                const PhysicsHierarchy::State &parent_state =
                    physics_hierarchy_states_by_actor_index_[*parent_actor_index];
                const ActorUID parent_uid = GetActorUID(*parent_actor_index);
                if (parent_state.has_dynamic_rigidbody_self) {
                    nearest_dynamic_ancestor_uid = parent_uid;
                } else {
                    nearest_dynamic_ancestor_uid =
                        parent_state.nearest_dynamic_body_ancestor_uid;
                }
            }

            state.nearest_dynamic_body_ancestor_uid =
                nearest_dynamic_ancestor_uid;
            state.is_under_dynamic_hierarchy =
                nearest_dynamic_ancestor_uid != PhysicsHierarchy::kInvalidActorUID;
            if (state.has_dynamic_rigidbody_self) {
                state.physics_root_uid = GetActorUID(actor_index);
            } else if (state.is_under_dynamic_hierarchy) {
                state.physics_root_uid = nearest_dynamic_ancestor_uid;
            }
            if (state.rigidbody_enabled_self &&
                state.requested_body_type == "dynamic" &&
                state.is_under_dynamic_hierarchy) {
                state.effective_body_type = "kinematic";
            }

            physics_hierarchy_states_by_actor_index_[actor_index] =
                std::move(state);
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
