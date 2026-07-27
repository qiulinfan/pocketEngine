#include "editor/ai/AIReadOnlyTools.h"
#include "editor/documents/SceneDocument.h"
#include "scripting/ComponentManager.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <variant>

namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

void WritePropertyValue(Writer &writer,
                        const Actor::ComponentPropertyValue &value) {
    std::visit(
        [&writer](const auto &typed_value) {
            using ValueType = std::decay_t<decltype(typed_value)>;
            if constexpr (std::is_same_v<ValueType, bool>) {
                writer.Bool(typed_value);
            } else if constexpr (std::is_same_v<ValueType, int>) {
                writer.Int(typed_value);
            } else if constexpr (std::is_same_v<ValueType, double>) {
                writer.Double(typed_value);
            } else if constexpr (std::is_same_v<ValueType, std::string>) {
                writer.String(typed_value.c_str());
            } else {
                writer.StartArray();
                for (const auto &element : typed_value) {
                    using ElementType =
                        typename ValueType::value_type;
                    if constexpr (std::is_same_v<ElementType, bool>) {
                        writer.Bool(element);
                    } else if constexpr (std::is_same_v<ElementType, int>) {
                        writer.Int(element);
                    } else if constexpr (std::is_same_v<ElementType, double>) {
                        writer.Double(element);
                    } else {
                        writer.String(element.c_str());
                    }
                }
                writer.EndArray();
            }
        },
        value);
}

void WriteProperties(
    Writer &writer,
    const std::vector<Actor::ComponentProperty> &properties) {
    writer.StartObject();
    for (const Actor::ComponentProperty &property : properties) {
        writer.Key(property.name.c_str());
        WritePropertyValue(writer, property.value);
    }
    writer.EndObject();
}

void WriteTransform(Writer &writer, const SceneDocument &scene,
                    std::size_t actor_index, bool world) {
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
    std::string component_key;
    const bool available =
        world ? scene.TryGetActorWorldTransform(
                    actor_index, &component_key, x, y, rotation)
              : scene.TryGetActorLocalTransform(
                    actor_index, &component_key, x, y, rotation);
    if (!available) {
        writer.Null();
        return;
    }
    writer.StartObject();
    writer.Key("component_key");
    writer.String(component_key.c_str());
    writer.Key("x");
    writer.Double(x);
    writer.Key("y");
    writer.Double(y);
    writer.Key("rotation");
    writer.Double(rotation);
    writer.EndObject();
}

bool ReadBool(const rapidjson::Value &arguments, const char *key,
              bool fallback) {
    return arguments.IsObject() && arguments.HasMember(key) &&
                   arguments[key].IsBool()
               ? arguments[key].GetBool()
               : fallback;
}

int ReadInt(const rapidjson::Value &arguments, const char *key,
            int fallback, int minimum, int maximum) {
    if (!arguments.IsObject() || !arguments.HasMember(key) ||
        !arguments[key].IsInt()) {
        return fallback;
    }
    return std::clamp(arguments[key].GetInt(), minimum, maximum);
}

std::string ReadString(const rapidjson::Value &arguments, const char *key) {
    if (!arguments.IsObject() || !arguments.HasMember(key) ||
        !arguments[key].IsString()) {
        return "";
    }
    return arguments[key].GetString();
}

bool RequireScene(const AIEditorContext &context, std::string &out_code,
                  std::string &out_message) {
    if (context.scene_document != nullptr) return true;
    out_code = "SCENE_NOT_LOADED";
    out_message = "The editor has not published a SceneDocument context yet.";
    return false;
}

bool ExecuteGetEditorState(const AIEditorContext &context,
                           std::string &out_json) {
    rapidjson::StringBuffer buffer;
    Writer writer(buffer);
    const SceneDocument &scene = *context.scene_document;
    writer.StartObject();
    writer.Key("project_root");
    writer.String(context.project_root.generic_string().c_str());
    writer.Key("scene");
    writer.StartObject();
    writer.Key("name");
    writer.String(scene.GetSceneName().c_str());
    writer.Key("path");
    writer.String(scene.GetScenePath().generic_string().c_str());
    writer.Key("actor_count");
    writer.Uint64(static_cast<std::uint64_t>(scene.GetActorCount()));
    writer.Key("dirty");
    writer.Bool(scene.IsDirty());
    writer.EndObject();
    writer.Key("mode");
    writer.String(context.play_mode_active
                      ? (context.play_mode_paused ? "play_paused" : "play")
                      : "edit");
    writer.Key("edit_mode_live_preview");
    writer.Bool(context.edit_mode_live_preview_enabled);
    writer.Key("selection");
    if (context.selected_actor_index >= 0 &&
        context.selected_actor_index <
            static_cast<int>(scene.GetActorCount())) {
        const std::size_t index =
            static_cast<std::size_t>(context.selected_actor_index);
        writer.StartObject();
        writer.Key("actor_uid");
        writer.Uint64(scene.GetActorUID(index));
        writer.Key("name");
        writer.String(scene.GetActorDisplayName(index).c_str());
        writer.Key("runtime_actor_uid");
        writer.Uint64(context.selected_runtime_actor_uid);
        writer.EndObject();
    } else {
        writer.Null();
    }
    writer.Key("capabilities");
    writer.StartArray();
    writer.String("read_scene");
    writer.String("inspect_actor");
    writer.String("list_component_types");
    writer.String("search_assets");
    writer.EndArray();
    writer.Key("writes_enabled");
    writer.Bool(false);
    writer.EndObject();
    out_json = buffer.GetString();
    return true;
}

bool ExecuteGetCurrentScene(const rapidjson::Value &arguments,
                            const AIEditorContext &context,
                            std::string &out_json) {
    const SceneDocument &scene = *context.scene_document;
    const bool include_components =
        ReadBool(arguments, "include_components", true);
    const std::size_t limit = static_cast<std::size_t>(
        ReadInt(arguments, "max_actors", 100, 1, 500));
    const std::size_t count = std::min(scene.GetActorCount(), limit);
    rapidjson::StringBuffer buffer;
    Writer writer(buffer);
    writer.StartObject();
    writer.Key("scene_name");
    writer.String(scene.GetSceneName().c_str());
    writer.Key("scene_path");
    writer.String(scene.GetScenePath().generic_string().c_str());
    writer.Key("dirty");
    writer.Bool(scene.IsDirty());
    writer.Key("actor_count");
    writer.Uint64(static_cast<std::uint64_t>(scene.GetActorCount()));
    writer.Key("truncated");
    writer.Bool(count < scene.GetActorCount());
    writer.Key("actors");
    writer.StartArray();
    for (std::size_t actor_index = 0; actor_index < count; ++actor_index) {
        const SceneDocument::ActorRecord &raw =
            scene.GetActorRecords()[actor_index];
        const Actor effective = scene.BuildEffectiveActor(actor_index);
        writer.StartObject();
        writer.Key("actor_uid");
        writer.Uint64(scene.GetActorUID(actor_index));
        writer.Key("name");
        writer.String(scene.GetActorDisplayName(actor_index).c_str());
        writer.Key("parent_uid");
        writer.Uint64(raw.parent_uid);
        writer.Key("template");
        if (raw.template_name.empty()) writer.Null();
        else writer.String(raw.template_name.c_str());
        if (include_components) {
            writer.Key("components");
            writer.StartArray();
            for (const Actor::ComponentSpec &component :
                 effective.component_specs) {
                writer.StartObject();
                writer.Key("key");
                writer.String(component.key.c_str());
                writer.Key("type");
                writer.String(component.type.c_str());
                writer.EndObject();
            }
            writer.EndArray();
        }
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
    out_json = buffer.GetString();
    return true;
}

bool ExecuteInspectActor(const rapidjson::Value &arguments,
                         const AIEditorContext &context,
                         std::string &out_json, std::string &out_code,
                         std::string &out_message) {
    if (!arguments.IsObject() || !arguments.HasMember("actor_uid") ||
        !arguments["actor_uid"].IsUint64()) {
        out_code = "MCP_INVALID_ARGUMENT";
        out_message = "actor_uid must be a non-negative integer.";
        return false;
    }
    const SceneDocument &scene = *context.scene_document;
    const SceneDocument::ActorUID actor_uid =
        arguments["actor_uid"].GetUint64();
    const std::optional<std::size_t> actor_index =
        scene.FindActorIndexByUID(actor_uid);
    if (!actor_index.has_value()) {
        out_code = "ACTOR_NOT_FOUND";
        out_message = "No authoring actor has UID " +
                      std::to_string(actor_uid) + ".";
        return false;
    }
    const SceneDocument::ActorRecord &raw =
        scene.GetActorRecords()[*actor_index];
    const Actor effective = scene.BuildEffectiveActor(*actor_index);
    const PhysicsHierarchy::State physics =
        scene.GetPhysicsHierarchyState(*actor_index);
    rapidjson::StringBuffer buffer;
    Writer writer(buffer);
    writer.StartObject();
    writer.Key("actor_uid");
    writer.Uint64(actor_uid);
    writer.Key("name");
    writer.String(scene.GetActorDisplayName(*actor_index).c_str());
    writer.Key("parent_uid");
    writer.Uint64(raw.parent_uid);
    writer.Key("template");
    if (raw.template_name.empty()) writer.Null();
    else writer.String(raw.template_name.c_str());
    writer.Key("local_transform");
    WriteTransform(writer, scene, *actor_index, false);
    writer.Key("world_transform");
    WriteTransform(writer, scene, *actor_index, true);
    writer.Key("physics_hierarchy");
    writer.StartObject();
    writer.Key("has_rigidbody_self");
    writer.Bool(physics.has_rigidbody_self);
    writer.Key("requested_body_type");
    writer.String(physics.requested_body_type.c_str());
    writer.Key("effective_body_type");
    writer.String(physics.effective_body_type.c_str());
    writer.Key("physics_root_uid");
    writer.Uint64(physics.physics_root_uid);
    writer.Key("nearest_dynamic_body_ancestor_uid");
    writer.Uint64(physics.nearest_dynamic_body_ancestor_uid);
    writer.EndObject();
    writer.Key("components");
    writer.StartArray();
    for (const Actor::ComponentSpec &component : effective.component_specs) {
        writer.StartObject();
        writer.Key("key");
        writer.String(component.key.c_str());
        writer.Key("type");
        writer.String(component.type.c_str());
        writer.Key("template_backed");
        writer.Bool(scene.IsTemplateBackedComponent(*actor_index,
                                                    component.key));
        writer.Key("properties");
        WriteProperties(
            writer,
            scene.GetInspectableProperties(*actor_index, component.key));
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
    out_json = buffer.GetString();
    return true;
}

bool ExecuteListComponentTypes(const rapidjson::Value &arguments,
                               std::string &out_json) {
    const std::string query = Lower(ReadString(arguments, "query"));
    const std::size_t limit = static_cast<std::size_t>(
        ReadInt(arguments, "limit", 100, 1, 500));
    const std::vector<std::string> types =
        ComponentManager::GetRegisteredComponentTypes();
    rapidjson::StringBuffer buffer;
    Writer writer(buffer);
    writer.StartObject();
    writer.Key("component_types");
    writer.StartArray();
    std::size_t written = 0;
    for (const std::string &type : types) {
        if (!query.empty() &&
            Lower(type).find(query) == std::string::npos) {
            continue;
        }
        if (written >= limit) break;
        writer.StartObject();
        writer.Key("type");
        writer.String(type.c_str());
        writer.Key("default_properties");
        WriteProperties(
            writer,
            ComponentManager::GetComponentTypeDefaultProperties(type));
        writer.EndObject();
        ++written;
    }
    writer.EndArray();
    writer.Key("returned");
    writer.Uint64(static_cast<std::uint64_t>(written));
    writer.EndObject();
    out_json = buffer.GetString();
    return true;
}

bool ExecuteSearchAssets(const rapidjson::Value &arguments,
                         const AIEditorContext &context,
                         std::string &out_json) {
    const std::string query = Lower(ReadString(arguments, "query"));
    std::string requested_type = Lower(ReadString(arguments, "type"));
    if (!requested_type.empty() && requested_type.front() == '.') {
        requested_type.erase(requested_type.begin());
    }
    const std::size_t limit = static_cast<std::size_t>(
        ReadInt(arguments, "limit", 50, 1, 200));
    std::vector<std::filesystem::path> matches;
    std::error_code error;
    const std::filesystem::path root =
        context.project_root.lexically_normal();
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && iterator != end && matches.size() < limit;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) continue;
        const std::filesystem::path relative =
            iterator->path().lexically_relative(root);
        const std::string relative_text = relative.generic_string();
        if (relative_text.empty() ||
            relative_text.rfind("../", 0) == 0) {
            continue;
        }
        const std::string lowered = Lower(relative_text);
        std::string extension = Lower(relative.extension().string());
        if (!extension.empty() && extension.front() == '.') {
            extension.erase(extension.begin());
        }
        if (!query.empty() &&
            lowered.find(query) == std::string::npos) {
            continue;
        }
        if (!requested_type.empty() && extension != requested_type &&
            lowered.find("/" + requested_type + "s/") ==
                std::string::npos) {
            continue;
        }
        matches.push_back(relative);
    }
    std::sort(matches.begin(), matches.end());
    rapidjson::StringBuffer buffer;
    Writer writer(buffer);
    writer.StartObject();
    writer.Key("project_root");
    writer.String(root.generic_string().c_str());
    writer.Key("assets");
    writer.StartArray();
    for (const std::filesystem::path &path : matches) {
        writer.StartObject();
        writer.Key("path");
        writer.String(path.generic_string().c_str());
        writer.Key("extension");
        writer.String(path.extension().string().c_str());
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("returned");
    writer.Uint64(static_cast<std::uint64_t>(matches.size()));
    writer.EndObject();
    out_json = buffer.GetString();
    return true;
}

} // namespace

bool AIReadOnlyTools::Execute(const std::string &tool,
                              const std::string &arguments_json,
                              const AIEditorContext &context,
                              std::string &out_result_json,
                              std::string &out_error_code,
                              std::string &out_error_message) {
    out_result_json.clear();
    out_error_code.clear();
    out_error_message.clear();
    if (!RequireScene(context, out_error_code, out_error_message)) {
        return false;
    }
    rapidjson::Document arguments;
    arguments.Parse(arguments_json.c_str());
    if (arguments.HasParseError() || !arguments.IsObject()) {
        out_error_code = "MCP_INVALID_ARGUMENT";
        out_error_message = "Tool arguments must be a JSON object.";
        return false;
    }
    if (tool == "get_editor_state") {
        return ExecuteGetEditorState(context, out_result_json);
    }
    if (tool == "get_current_scene") {
        return ExecuteGetCurrentScene(arguments, context, out_result_json);
    }
    if (tool == "inspect_actor") {
        return ExecuteInspectActor(arguments, context, out_result_json,
                                   out_error_code, out_error_message);
    }
    if (tool == "list_component_types") {
        return ExecuteListComponentTypes(arguments, out_result_json);
    }
    if (tool == "search_assets") {
        return ExecuteSearchAssets(arguments, context, out_result_json);
    }
    out_error_code = "MCP_TOOL_NOT_FOUND";
    out_error_message = "Unknown read-only tool: " + tool;
    return false;
}
