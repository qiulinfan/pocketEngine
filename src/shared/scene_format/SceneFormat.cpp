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
#include <sstream>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace SceneFormat {
namespace {

constexpr const char *kArrayTypeKey = "__array_type";
constexpr const char *kArrayItemsKey = "items";

std::filesystem::path SceneRootPath() {
    return ResourcePath::ResourceSubdirectory("scenes");
}

std::filesystem::path TemplateRootPath() {
    return ResourcePath::ResourceSubdirectory("actor_templates");
}

enum class PropertyArrayType {
    Bool,
    Int,
    Double,
    String,
    Unknown
};

PropertyArrayType DetermineArrayTypeFromString(const std::string &type_name) {
    if (type_name == "bool") return PropertyArrayType::Bool;
    if (type_name == "int") return PropertyArrayType::Int;
    if (type_name == "double") return PropertyArrayType::Double;
    if (type_name == "string") return PropertyArrayType::String;
    return PropertyArrayType::Unknown;
}

const char *GetArrayTypeName(PropertyArrayType array_type) {
    switch (array_type) {
    case PropertyArrayType::Bool:
        return "bool";
    case PropertyArrayType::Int:
        return "int";
    case PropertyArrayType::Double:
        return "double";
    case PropertyArrayType::String:
        return "string";
    case PropertyArrayType::Unknown:
    default:
        return "";
    }
}

PropertyArrayType DetermineArrayTypeFromFirstValue(const rapidjson::Value &value) {
    if (value.IsBool()) return PropertyArrayType::Bool;
    if (value.IsString()) return PropertyArrayType::String;
    if (value.IsInt64() || value.IsUint64()) return PropertyArrayType::Int;
    if (value.IsDouble()) return PropertyArrayType::Double;
    return PropertyArrayType::Unknown;
}

bool TryParseArrayValue(const rapidjson::Value &value,
                        PropertyArrayType array_type,
                        Actor::ComponentPropertyValue &out_value) {
    if (!value.IsArray()) return false;

    switch (array_type) {
    case PropertyArrayType::Bool: {
        Actor::BoolArray values;
        values.reserve(value.Size());
        for (rapidjson::SizeType index = 0; index < value.Size(); ++index) {
            if (!value[index].IsBool()) return false;
            values.emplace_back(value[index].GetBool());
        }
        out_value = std::move(values);
        return true;
    }
    case PropertyArrayType::Int: {
        Actor::IntArray values;
        values.reserve(value.Size());
        for (rapidjson::SizeType index = 0; index < value.Size(); ++index) {
            const rapidjson::Value &element = value[index];
            if (element.IsInt64()) {
                const int64_t i64 = element.GetInt64();
                if (i64 < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
                    i64 > static_cast<int64_t>(std::numeric_limits<int>::max())) {
                    return false;
                }
                values.emplace_back(static_cast<int>(i64));
                continue;
            }
            if (element.IsUint64()) {
                const uint64_t u64 = element.GetUint64();
                if (u64 > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
                    return false;
                }
                values.emplace_back(static_cast<int>(u64));
                continue;
            }
            return false;
        }
        out_value = std::move(values);
        return true;
    }
    case PropertyArrayType::Double: {
        Actor::DoubleArray values;
        values.reserve(value.Size());
        for (rapidjson::SizeType index = 0; index < value.Size(); ++index) {
            if (!value[index].IsNumber()) return false;
            values.emplace_back(value[index].GetDouble());
        }
        out_value = std::move(values);
        return true;
    }
    case PropertyArrayType::String: {
        Actor::StringArray values;
        values.reserve(value.Size());
        for (rapidjson::SizeType index = 0; index < value.Size(); ++index) {
            if (!value[index].IsString()) return false;
            values.emplace_back(value[index].GetString());
        }
        out_value = std::move(values);
        return true;
    }
    case PropertyArrayType::Unknown:
    default:
        return false;
    }
}

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
    if (value.IsArray()) {
        if (value.Empty()) return false;
        return TryParseArrayValue(value, DetermineArrayTypeFromFirstValue(value[0]),
                                  out_value);
    }
    if (value.IsObject()) {
        if (!value.HasMember(kArrayTypeKey) || !value[kArrayTypeKey].IsString()) {
            return false;
        }
        if (!value.HasMember(kArrayItemsKey)) return false;
        const PropertyArrayType array_type =
            DetermineArrayTypeFromString(value[kArrayTypeKey].GetString());
        return TryParseArrayValue(value[kArrayItemsKey], array_type, out_value);
    }
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
            } else if constexpr (std::is_same_v<ValueType, Actor::BoolArray>) {
                if (typed_value.empty()) {
                    writer.StartObject();
                    writer.Key(kArrayTypeKey);
                    writer.String(GetArrayTypeName(PropertyArrayType::Bool));
                    writer.Key(kArrayItemsKey);
                    writer.StartArray();
                    writer.EndArray();
                    writer.EndObject();
                    return;
                }
                writer.StartArray();
                for (bool element : typed_value) {
                    writer.Bool(element);
                }
                writer.EndArray();
            } else if constexpr (std::is_same_v<ValueType, Actor::IntArray>) {
                if (typed_value.empty()) {
                    writer.StartObject();
                    writer.Key(kArrayTypeKey);
                    writer.String(GetArrayTypeName(PropertyArrayType::Int));
                    writer.Key(kArrayItemsKey);
                    writer.StartArray();
                    writer.EndArray();
                    writer.EndObject();
                    return;
                }
                writer.StartArray();
                for (int element : typed_value) {
                    writer.Int(element);
                }
                writer.EndArray();
            } else if constexpr (std::is_same_v<ValueType, Actor::DoubleArray>) {
                if (typed_value.empty()) {
                    writer.StartObject();
                    writer.Key(kArrayTypeKey);
                    writer.String(GetArrayTypeName(PropertyArrayType::Double));
                    writer.Key(kArrayItemsKey);
                    writer.StartArray();
                    writer.EndArray();
                    writer.EndObject();
                    return;
                }
                writer.StartArray();
                for (double element : typed_value) {
                    writer.Double(element);
                }
                writer.EndArray();
            } else if constexpr (std::is_same_v<ValueType, Actor::StringArray>) {
                if (typed_value.empty()) {
                    writer.StartObject();
                    writer.Key(kArrayTypeKey);
                    writer.String(GetArrayTypeName(PropertyArrayType::String));
                    writer.Key(kArrayItemsKey);
                    writer.StartArray();
                    writer.EndArray();
                    writer.EndObject();
                    return;
                }
                writer.StartArray();
                for (const std::string &element : typed_value) {
                    writer.String(element.c_str());
                }
                writer.EndArray();
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
        actor_record.uid = actor_json["uid"].GetUint64();
    }
    if (actor_json.HasMember("parent_uid") &&
        actor_json["parent_uid"].IsUint64()) {
        actor_record.parent_uid = actor_json["parent_uid"].GetUint64();
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
    return ResourcePath::ResolveResourcePath(SceneRootPath(), scene_name, {".scene"},
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
    const std::filesystem::path scene_root = SceneRootPath();
    scene_asset.scene_subdirectory = ResourcePath::NormalizeRelativePath(
        std::filesystem::relative(scene_asset.scene_path.parent_path(), scene_root));

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
        if (actor_record.uid != 0) {
            writer.Key("uid");
            writer.Uint64(actor_record.uid);
        }
        if (actor_record.parent_uid != 0) {
            writer.Key("parent_uid");
            writer.Uint64(actor_record.parent_uid);
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
    return ResourcePath::ResolveResourcePath(TemplateRootPath(), template_name,
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
    const std::filesystem::path template_root = TemplateRootPath();
    template_asset.template_subdirectory = ResourcePath::NormalizeRelativePath(
        std::filesystem::relative(template_asset.template_path.parent_path(),
                                  template_root));

    rapidjson::Document template_document;
    ReadJsonFile(template_asset.template_path, template_document);
    if (!template_document.IsObject()) {
        return template_asset;
    }

    template_asset.actor = ApplyActorRecordToActor(Actor(), ParseActorRecord(template_document));
    return template_asset;
}

bool ValidateSceneAssetForRuntime(const SceneAsset &scene_asset,
                                  std::string *out_error) {
    if (scene_asset.scene_path.empty()) {
        if (out_error != nullptr) {
            *out_error = "scene " + scene_asset.scene_name + " is missing";
        }
        return false;
    }

    for (std::size_t actor_index = 0; actor_index < scene_asset.actors.size();
         ++actor_index) {
        const ActorRecord &actor_record = scene_asset.actors[actor_index];
        if (actor_record.template_name.empty()) continue;
        if (!ResolveTemplatePath(actor_record.template_name,
                                 scene_asset.scene_subdirectory).empty()) {
            continue;
        }

        if (out_error != nullptr) {
            std::ostringstream message;
            message << "scene " << scene_asset.scene_name << " actor ";
            if (!actor_record.name.empty()) {
                message << "\"" << actor_record.name << "\"";
            } else {
                message << "#" << (actor_index + 1);
            }
            message << " references missing template \""
                    << actor_record.template_name << "\"";
            *out_error = message.str();
        }
        return false;
    }

    return true;
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
    actor.uid = actor_record.uid;
    actor.scene_backed = true;
    actor.parent_uid = actor_record.parent_uid;
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
    actor.runtime_destroyed = false;
    actor.dont_destroy_on_scene_load = false;
    return actor;
}

Actor BuildEditableActor(const ActorRecord &actor_record,
                         const std::filesystem::path &scene_subdirectory,
                         std::string *out_warning) {
    Scene::SetActiveSceneSubdirectory(scene_subdirectory);

    Actor actor;
    if (!actor_record.template_name.empty()) {
        const ActorTemplateAsset template_asset =
            LoadActorTemplateAsset(actor_record.template_name,
                                   scene_subdirectory);
        if (template_asset.template_path.empty()) {
            if (out_warning != nullptr) {
                *out_warning = "missing template " + actor_record.template_name;
            }
        } else {
            actor = template_asset.actor;
        }
    }

    actor = ApplyActorRecordToActor(std::move(actor), actor_record);
    actor.runtime_destroyed = false;
    actor.dont_destroy_on_scene_load = false;
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
