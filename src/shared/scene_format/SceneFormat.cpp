#include "shared/scene_format/SceneFormat.h"
#include "scene/Scene.h"
#include "shared/resources/ResourcePath.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace SceneFormat {
namespace {

constexpr const char *kSceneRoot = "resources/scenes";
constexpr const char *kTemplateRoot = "resources/actor_templates";

void ReadJsonFile(const std::filesystem::path &path,
                  rapidjson::Document &out_document) {
    FILE *file_pointer = nullptr;
#ifdef _WIN32
    fopen_s(&file_pointer, path.string().c_str(), "rb");
#else
    file_pointer = fopen(path.string().c_str(), "rb");
#endif
    if (file_pointer == nullptr) {
        std::cout << "error: unable to open [" << path.string() << "]"
                  << std::endl;
        std::exit(0);
    }

    char buffer[65536];
    rapidjson::FileReadStream stream(file_pointer, buffer, sizeof(buffer));
    out_document.ParseStream(stream);
    std::fclose(file_pointer);

    if (out_document.HasParseError()) {
        std::cout << "error parsing json at [" << path.string() << "]"
                  << std::endl;
        std::exit(0);
    }
}

bool TryParseComponentPropertyValue(const rapidjson::Value &value,
                                    Actor::ComponentPropertyValue &out_value) {
    if (value.IsString()) {
        out_value = std::string(value.GetString());
        return true;
    }
    if (value.IsBool()) {
        out_value = value.GetBool();
        return true;
    }
    if (value.IsInt64()) {
        const int64_t i64 = value.GetInt64();
        if (i64 >= static_cast<int64_t>(std::numeric_limits<int>::min()) &&
            i64 <= static_cast<int64_t>(std::numeric_limits<int>::max())) {
            out_value = static_cast<int>(i64);
        } else {
            out_value = static_cast<double>(i64);
        }
        return true;
    }
    if (value.IsUint64()) {
        const uint64_t u64 = value.GetUint64();
        if (u64 <= static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            out_value = static_cast<int>(u64);
        } else {
            out_value = static_cast<double>(u64);
        }
        return true;
    }
    if (value.IsDouble()) {
        out_value = value.GetDouble();
        return true;
    }
    return false;
}

void WritePropertyValue(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer,
                        const Actor::ComponentPropertyValue &value) {
    std::visit(
        [&](const auto &typed_value) {
            using ValueType = std::decay_t<decltype(typed_value)>;
            if constexpr (std::is_same_v<ValueType, bool>) {
                writer.Bool(typed_value);
            } else if constexpr (std::is_same_v<ValueType, int>) {
                writer.Int(typed_value);
            } else if constexpr (std::is_same_v<ValueType, double>) {
                writer.Double(typed_value);
            } else if constexpr (std::is_same_v<ValueType, std::string>) {
                writer.String(typed_value.c_str());
            }
        },
        value);
}

void ApplyComponentObjectOverrides(Actor::ComponentSpec &component_spec,
                                   const rapidjson::Value &component_object) {
    if (component_object.HasMember("type") &&
        component_object["type"].IsString()) {
        component_spec.type = component_object["type"].GetString();
    }

    for (auto member_it = component_object.MemberBegin();
         member_it != component_object.MemberEnd(); ++member_it) {
        if (!member_it->name.IsString()) continue;
        const std::string property_name = member_it->name.GetString();
        if (property_name == "type") continue;

        Actor::ComponentPropertyValue property_value;
        if (!TryParseComponentPropertyValue(member_it->value, property_value)) {
            continue;
        }
        UpsertComponentProperty(component_spec, property_name, property_value);
    }
}

ActorRecord ParseActorRecord(const rapidjson::Value &actor_json) {
    ActorRecord actor_record;

    if (actor_json.HasMember("uid") && actor_json["uid"].IsUint64()) {
        actor_record.editor_actor_uid = actor_json["uid"].GetUint64();
    }
    if (actor_json.HasMember("parent_uid") &&
        actor_json["parent_uid"].IsUint64()) {
        actor_record.parent_actor_uid = actor_json["parent_uid"].GetUint64();
    }
    if (actor_json.HasMember("template") && actor_json["template"].IsString()) {
        actor_record.template_name = actor_json["template"].GetString();
    }
    if (actor_json.HasMember("name") && actor_json["name"].IsString()) {
        actor_record.name = actor_json["name"].GetString();
    }
    if (actor_json.HasMember("components") && actor_json["components"].IsObject()) {
        const rapidjson::Value &components_object = actor_json["components"];
        actor_record.component_specs.reserve(components_object.MemberCount());

        for (auto it = components_object.MemberBegin();
             it != components_object.MemberEnd(); ++it) {
            if (!it->name.IsString()) continue;
            if (!it->value.IsObject()) continue;

            Actor::ComponentSpec component_spec;
            component_spec.key = it->name.GetString();
            ApplyComponentObjectOverrides(component_spec, it->value);
            actor_record.component_specs.emplace_back(std::move(component_spec));
        }
    }

    SortComponentSpecs(actor_record.component_specs);
    return actor_record;
}

} // namespace

// -----------------------------------------------------------------------------
// Scene assets: raw .scene file loading / saving
// -----------------------------------------------------------------------------

// Resolve a scene name to a concrete .scene path on disk.
std::string ResolveScenePath(
    const std::string &scene_name,
    const std::filesystem::path &preferred_subdirectory) {
    return ResourcePath::ResolveResourcePath(kSceneRoot, scene_name, {".scene"},
                                             preferred_subdirectory);
}

// Load one .scene file into a raw asset representation.
SceneAsset LoadSceneAsset(const std::string &scene_name,
                          const std::filesystem::path &preferred_subdirectory) {
    SceneAsset scene_asset;
    scene_asset.scene_name = scene_name;

    const std::string resolved_scene_path = ResolveScenePath(scene_name, preferred_subdirectory);
    if (resolved_scene_path.empty()) {
        return scene_asset;
    }

    scene_asset.scene_path = resolved_scene_path;
    scene_asset.scene_subdirectory = ResourcePath::NormalizeRelativePath(
        std::filesystem::relative(scene_asset.scene_path.parent_path(), kSceneRoot));

    rapidjson::Document scene_document;
    ReadJsonFile(scene_asset.scene_path, scene_document);

    if (!scene_document.HasMember("actors") || !scene_document["actors"].IsArray()) {
        return scene_asset;
    }

    const rapidjson::Value &actors_array = scene_document["actors"];
    scene_asset.actors.reserve(actors_array.Size());
    for (rapidjson::SizeType i = 0; i < actors_array.Size(); ++i) {
        if (!actors_array[i].IsObject()) continue;
        scene_asset.actors.emplace_back(ParseActorRecord(actors_array[i]));
    }

    return scene_asset;
}

// Save one raw scene asset back into its source .scene file.
bool SaveSceneAsset(const SceneAsset &scene_asset) {
    if (scene_asset.scene_path.empty()) return false;

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();
    writer.Key("actors");
    writer.StartArray();
    for (const ActorRecord &actor_record : scene_asset.actors) {
        writer.StartObject();
        if (actor_record.editor_actor_uid != 0) {
            writer.Key("uid");
            writer.Uint64(actor_record.editor_actor_uid);
        }
        if (actor_record.parent_actor_uid != 0) {
            writer.Key("parent_uid");
            writer.Uint64(actor_record.parent_actor_uid);
        }
        if (!actor_record.template_name.empty()) {
            writer.Key("template");
            writer.String(actor_record.template_name.c_str());
        }
        if (!actor_record.name.empty()) {
            writer.Key("name");
            writer.String(actor_record.name.c_str());
        }

        writer.Key("components");
        writer.StartObject();
        for (const Actor::ComponentSpec &component_spec :
             actor_record.component_specs) {
            writer.Key(component_spec.key.c_str());
            writer.StartObject();
            if (!component_spec.type.empty()) {
                writer.Key("type");
                writer.String(component_spec.type.c_str());
            }
            for (const Actor::ComponentProperty &property :
                 component_spec.overrides) {
                writer.Key(property.name.c_str());
                WritePropertyValue(writer, property.value);
            }
            writer.EndObject();
        }
        writer.EndObject();

        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();

    std::ofstream output_file(scene_asset.scene_path,
                              std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
        std::cout << "error: unable to write [" << scene_asset.scene_path.string()
                  << "]" << std::endl;
        std::exit(0);
    }

    output_file << buffer.GetString() << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Template assets: raw .template file loading
// -----------------------------------------------------------------------------

// Resolve a template name to a concrete .template path on disk.
std::string ResolveTemplatePath(
    const std::string &template_name,
    const std::filesystem::path &preferred_subdirectory) {
    return ResourcePath::ResolveResourcePath(kTemplateRoot, template_name,
                                             {".template"},
                                             preferred_subdirectory);
}

// Load one actor template file into a parsed asset representation.
ActorTemplateAsset LoadActorTemplateAsset(
    const std::string &template_name,
    const std::filesystem::path &preferred_subdirectory) {
    ActorTemplateAsset template_asset;
    template_asset.template_name = template_name;

    const std::string resolved_template_path =
        ResolveTemplatePath(template_name, preferred_subdirectory);
    if (resolved_template_path.empty()) {
        return template_asset;
    }

    template_asset.template_path = resolved_template_path;
    template_asset.template_subdirectory = ResourcePath::NormalizeRelativePath(
        std::filesystem::relative(template_asset.template_path.parent_path(),
                                  kTemplateRoot));

    rapidjson::Document template_document;
    ReadJsonFile(template_asset.template_path, template_document);
    if (!template_document.IsObject()) {
        return template_asset;
    }

    template_asset.actor = ApplyActorRecordToActor(Actor(), ParseActorRecord(template_document));
    return template_asset;
}

// -----------------------------------------------------------------------------
// Shared actor/component merge helpers
// -----------------------------------------------------------------------------

// Shared component-spec helpers used by both runtime and editor code paths.
void SortComponentSpecs(std::vector<Actor::ComponentSpec> &component_specs) {
    std::sort(component_specs.begin(), component_specs.end(),
              [](const Actor::ComponentSpec &a,
                 const Actor::ComponentSpec &b) { return a.key < b.key; });
}

// Shared component-spec helpers used by both runtime and editor code paths.
bool HasComponentType(const std::vector<Actor::ComponentSpec> &component_specs,
                      const std::string &type_name) {
    return std::any_of(component_specs.begin(), component_specs.end(),
                       [&](const Actor::ComponentSpec &component_spec) {
                           return component_spec.type == type_name;
                       });
}

// Shared component-spec helpers used by both runtime and editor code paths.
void EnsureBuiltinTransformComponent( std::vector<Actor::ComponentSpec> &component_specs) {
    if (HasComponentType(component_specs, "Transform")) return;

    std::unordered_set<std::string> used_component_keys;
    used_component_keys.reserve(component_specs.size());
    for (const Actor::ComponentSpec &component_spec : component_specs) {
        used_component_keys.insert(component_spec.key);
    }

    int next_numeric_key = 1;
    while (used_component_keys.find(std::to_string(next_numeric_key)) !=
           used_component_keys.end()) {
        ++next_numeric_key;
    }

    Actor::ComponentSpec transform_component;
    transform_component.key = std::to_string(next_numeric_key);
    transform_component.type = "Transform";
    component_specs.emplace_back(std::move(transform_component));
    SortComponentSpecs(component_specs);
}

// Shared component-spec helpers used by both runtime and editor code paths.
Actor::ComponentSpec *FindComponentSpec(
    std::vector<Actor::ComponentSpec> &component_specs,
    const std::string &component_key) {
    for (Actor::ComponentSpec &component_spec : component_specs) {
        if (component_spec.key == component_key) return &component_spec;
    }
    return nullptr;
}

// Shared component-spec helpers used by both runtime and editor code paths.
const Actor::ComponentSpec *FindComponentSpec(
    const std::vector<Actor::ComponentSpec> &component_specs,
    const std::string &component_key) {
    for (const Actor::ComponentSpec &component_spec : component_specs) {
        if (component_spec.key == component_key) return &component_spec;
    }
    return nullptr;
}

// Shared component-spec helpers used by both runtime and editor code paths.
Actor::ComponentSpec &FindOrCreateComponentSpec(
    std::vector<Actor::ComponentSpec> &component_specs,
    const std::string &component_key) {
        Actor::ComponentSpec *existing = FindComponentSpec(component_specs, component_key);
    if (existing != nullptr) return *existing;

    Actor::ComponentSpec component_spec;
    component_spec.key = component_key;
    component_specs.emplace_back(std::move(component_spec));
    return component_specs.back();
}

// Shared component-spec helpers used by both runtime and editor code paths.
void UpsertComponentProperty(
    Actor::ComponentSpec &component_spec, const std::string &property_name,
    const Actor::ComponentPropertyValue &property_value) {
    for (Actor::ComponentProperty &property : component_spec.overrides) {
        if (property.name != property_name) continue;
        property.value = property_value;
        return;
    }

    Actor::ComponentProperty property;
    property.name = property_name;
    property.value = property_value;
    component_spec.overrides.emplace_back(std::move(property));
}

// Merge one raw scene actor record into an existing actor instance.
Actor ApplyActorRecordToActor(Actor actor, const ActorRecord &actor_record) {
    actor.editor_actor_uid = actor_record.editor_actor_uid;
    actor.scene_backed = true;
    actor.parent_editor_actor_uid = actor_record.parent_actor_uid;
    actor.parent_id = -1;
    if (!actor_record.name.empty()) {
        actor.actor_name = actor_record.name;
    }

    for (const Actor::ComponentSpec &raw_component_spec : actor_record.component_specs) {
        if (raw_component_spec.type == kDeletedComponentType) {
            actor.component_specs.erase(
                            std::remove_if(actor.component_specs.begin(), actor.component_specs.end(),
                               [&](const Actor::ComponentSpec &component_spec) {
                                   return component_spec.key == raw_component_spec.key;
                               }
                            ),
                actor.component_specs.end());
            continue;
        }
        Actor::ComponentSpec &component_spec = FindOrCreateComponentSpec(actor.component_specs, raw_component_spec.key);
        if (!raw_component_spec.type.empty()) {
            component_spec.type = raw_component_spec.type;
        }
        for (const Actor::ComponentProperty &property: raw_component_spec.overrides) {
            UpsertComponentProperty(component_spec, property.name, property.value);
        }
    }

    SortComponentSpecs(actor.component_specs);
    return actor;
}

// Build one fully merged actor exactly as runtime would see it.
Actor BuildEffectiveActor(const ActorRecord &actor_record,
                          const std::filesystem::path &scene_subdirectory) {
    // Build the actor exactly as runtime would see it: template defaults first,
    // then scene-level overrides on top.
    Scene::SetActiveSceneSubdirectory(scene_subdirectory);

    Actor actor;
    if (!actor_record.template_name.empty()) {
        actor = Actor::LoadTemplate(actor_record.template_name);
    }

    actor = ApplyActorRecordToActor(std::move(actor), actor_record);
    actor.id = -1;
    actor.runtime_destroyed = false;
    actor.dont_destroy_on_scene_load = false;
    actor.parent_id = -1;
    return actor;
}

// Build merged runtime actors from a raw scene asset.
std::vector<Actor> BuildRuntimeActors(const SceneAsset &scene_asset) {
    std::vector<Actor> runtime_actors;
    runtime_actors.reserve(scene_asset.actors.size());
    for (const ActorRecord &actor_record : scene_asset.actors) {
        runtime_actors.emplace_back(
            BuildEffectiveActor(actor_record, scene_asset.scene_subdirectory));
    }
    return runtime_actors;
}

} // namespace SceneFormat
