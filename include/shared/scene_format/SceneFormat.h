#ifndef SCENE_FORMAT_H
#define SCENE_FORMAT_H

#include "scene/Actor.h"
#include <filesystem>
#include <string>
#include <vector>

namespace SceneFormat {

// Editor-only tombstone type that marks one inherited component as deleted from
// a scene override record. Runtime never instantiates this pseudo type.
inline constexpr const char *kDeletedComponentType = "__editor_deleted__";

// Raw actor record exactly as it appears in a .scene file before template
// inheritance is applied.
struct ActorRecord {
    std::uint64_t uid = 0;
    std::uint64_t parent_uid = 0;
    std::string template_name;
    std::string name;
    std::vector<Actor::ComponentSpec> component_specs;
};

// Parsed scene asset shared by runtime loading and editor documents.
struct SceneAsset {
    std::string scene_name;
    std::filesystem::path scene_path;
    std::filesystem::path scene_subdirectory;
    std::vector<ActorRecord> actors;
};

// Parsed actor-template asset shared by runtime scene loading and editor
// effective-actor reconstruction.
struct ActorTemplateAsset {
    std::string template_name;
    std::filesystem::path template_path;
    std::filesystem::path template_subdirectory;
    Actor actor;
};

// Resolve a scene name to a concrete .scene path on disk
std::string ResolveScenePath(const std::string &scene_name, const std::filesystem::path &preferred_subdirectory = {});

// Load one .scene file into a raw asset representation
SceneAsset LoadSceneAsset(const std::string &scene_name, const std::filesystem::path &preferred_subdirectory = {});

// Save one raw scene asset back into its source .scene file
bool SaveSceneAsset(const SceneAsset &scene_asset);

// Resolve a template name to a concrete .template path on disk
std::string ResolveTemplatePath(const std::string &template_name, const std::filesystem::path &preferred_subdirectory = {});

// Load one actor template file into a parsed asset representation
ActorTemplateAsset LoadActorTemplateAsset(const std::string &template_name, const std::filesystem::path &preferred_subdirectory = {});
// Validate that runtime-strict scene loading can resolve all required assets.
bool ValidateSceneAssetForRuntime(const SceneAsset &scene_asset,
                                  std::string *out_error = nullptr);

// Shared component-spec helpers used by both runtime and editor code paths
void SortComponentSpecs(std::vector<Actor::ComponentSpec> &component_specs);
bool HasComponentType(const std::vector<Actor::ComponentSpec> &component_specs,
                      const std::string &type_name);
void EnsureBuiltinTransformComponent( std::vector<Actor::ComponentSpec> &component_specs);
Actor::ComponentSpec *FindComponentSpec(std::vector<Actor::ComponentSpec> &component_specs, const std::string &component_key);
const Actor::ComponentSpec *FindComponentSpec(const std::vector<Actor::ComponentSpec> &component_specs, const std::string &component_key);
Actor::ComponentSpec &FindOrCreateComponentSpec(std::vector<Actor::ComponentSpec> &component_specs, const std::string &component_key);
void UpsertComponentProperty(Actor::ComponentSpec &component_spec, const std::string &property_name, const Actor::ComponentPropertyValue &property_value);

/*
Merge one raw scene actor record into an existing actor instance. Callers can
pass an empty Actor() for non-template actors, or a preloaded template actor
when they want scene overrides layered over template defaults.
*/
Actor ApplyActorRecordToActor(Actor actor, const ActorRecord &actor_record);
// Build one fully merged actor exactly as runtime would see it.
Actor BuildEffectiveActor(const ActorRecord &actor_record,
                          const std::filesystem::path &scene_subdirectory);
// Build an editor-facing actor view without aborting when inherited assets are missing.
Actor BuildEditableActor(const ActorRecord &actor_record,
                         const std::filesystem::path &scene_subdirectory,
                         std::string *out_warning = nullptr);
// Build merged runtime actors from a raw scene asset.
std::vector<Actor> BuildRuntimeActors(const SceneAsset &scene_asset);

} // namespace SceneFormat

#endif
