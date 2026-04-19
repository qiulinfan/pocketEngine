#include "core/Engine.h"
#include "audio/AudioManager.h"
#include "core/FrameClock.h"
#include "scripting/ComponentManager.h"
#include "particles/ParticleManager.h"
#include "rendering/Renderer.h"
#include "rendering/SDLRenderHelper.h"
#include "scene/Scene.h"
#include "input/Input.h"
#include "input/SDLEventHelper.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include "SDL2_image/SDL_image.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <unordered_set>
#include <type_traits>

namespace {

// Builtin components are reconstructed from serialized specs when their type or
// properties change, because they do not use the generic Lua property patch path.
bool IsBuiltinRuntimeComponentType(const std::string &type_name) {
    return type_name == "Rigidbody" || type_name == "ParticleSystem" ||
           type_name == "Transform" || type_name == "SpriteRenderer";
}

bool CanBuiltinRuntimeComponentPatchInPlace(const std::string &type_name) {
    return type_name == "Transform" || type_name == "SpriteRenderer";
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

std::string BuildUniqueRuntimeActorName(const std::deque<Actor> &actors,
                                        const std::string &base_name) {
    const std::string safe_base_name =
        base_name.empty() ? "New Actor" : base_name;
    std::unordered_set<std::string> used_names;
    used_names.reserve(actors.size());
    for (const Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        used_names.insert(actor.actor_name);
    }

    if (used_names.find(safe_base_name) == used_names.end()) {
        return safe_base_name;
    }

    int suffix = 1;
    while (true) {
        const std::string candidate =
            safe_base_name + " (" + std::to_string(suffix) + ")";
        if (used_names.find(candidate) == used_names.end()) {
            return candidate;
        }
        ++suffix;
    }
}

bool TryReadRuntimePropertyAsBool(
    const std::vector<Actor::ComponentProperty> &properties,
    const std::string &property_name, bool &out_value) {
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

bool TryReadRuntimePropertyAsString(
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

PhysicsHierarchy::State BuildRuntimeActorPhysicsSelfState( int actor_id, const std::vector<Actor::ComponentSpec> &component_specs) {
    PhysicsHierarchy::State state;
    for (const Actor::ComponentSpec &component_spec : component_specs) {
        if (component_spec.type != "Rigidbody") continue;

        state.has_rigidbody_self = true;
        state.rigidbody_component_key = component_spec.key;

        const std::vector<Actor::ComponentProperty> properties =
            ComponentManager::GetRuntimeComponentProperties(actor_id,
                                                           component_spec.key);
        bool enabled = true;
        TryReadRuntimePropertyAsBool(properties, "enabled", enabled);
        state.rigidbody_enabled_self = enabled;

        std::string requested_body_type = "dynamic";
        TryReadRuntimePropertyAsString(properties, "body_type",
                                       requested_body_type);
        state.requested_body_type = requested_body_type;
        state.has_dynamic_rigidbody_self =
            enabled && requested_body_type == "dynamic";
        state.effective_body_type =
            enabled ? requested_body_type : PhysicsHierarchy::kNoBodyType;
        break;
    }
    return state;
}

} // namespace

Engine::Engine() {
    // Engine owns both standalone runtime and editor-embedded runtime state.
    // Construction loads config and prepares the initial actor/component world,
    // but SDL/window resources are created later by InitializeRuntime().
    config_ = GameConfig::Read();
    current_scene_name = config_.initial_scene_name;
    // initialize Lua runtime before scene load 
    // initial scene components then can be instantiated immediately and run OnStart on frame 0
    ComponentManager::Initialize();
    ComponentManager::BindEngine(this);
    ComponentManager::ClearActorComponents();
    if (!config_.initial_scene_name.empty()) {
        std::vector<Actor> loaded_actors = Scene::LoadScene(config_.initial_scene_name);
        actors.clear();
        for (Actor &actor : loaded_actors) {
            actors.emplace_back(std::move(actor));
        }
        ComponentManager::BindActorsForScene(actors);
        rebuildRuntimeActorUIDMap();
    } else {
        ComponentManager::BindActorsForScene(actors);
        rebuildRuntimeActorUIDMap();
    }
}

Engine::~Engine() {
    ShutdownRuntime();
}

void Engine::GameLoop() {
    InitializeRuntime();

    while (IsRunning()) {
        RunSingleFrame();
        PresentFrame();
    }

    ShutdownRuntime();
}

/*
Editor can call this without entering GameLoop()
so the host can bootstrap runtime services on demand
*/
void Engine::InitializeRuntime() {
    if (window != nullptr || renderer != nullptr) return;
    is_game_running = true;
    initialize();
}

/*
Tear down SDL objects and runtime only caches
They need to be cleared once actors and components are destroyed.
*/
void Engine::ShutdownRuntime() {
    
    if (window == nullptr && renderer == nullptr) return;

    destroyRuntimeRenderTarget();
    destroyScenePreviewRenderTarget();
    runtime_actor_by_editor_uid_.clear();
    ComponentManager::Shutdown();
    ParticleManager::Clear();
    Renderer::Shutdown();
    IMG_Quit();
    if (renderer != nullptr) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window != nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

/*
Just wrapper, checking if quit is requested this frame,
then call the real RunSingleFrame(bool quit_requested_this_frame)
*/
void Engine::RunSingleFrame() {
    if (!IsRunning()) return;

    bool quit_requested_this_frame = false;
    SDL_Event event;
    // check if this frame calls for a quit,
    while (SDLEventHelper::SDL_PollEvent(&event)) {
        ProcessSDLEvent(event, quit_requested_this_frame);
    }
    RunSingleFrame(quit_requested_this_frame);
}

void Engine::RunSingleFrame(bool quit_requested_this_frame) {
    if (!IsRunning()) return;
    // one round of game loop: scene load, OnStarts, update, render, late update
    processPendingSceneLoad();
    ComponentManager::ApplyEffectiveRigidbodyBodyTypes();
    ComponentManager::ProcessPendingOnStart();
    update();
    render();
    RecordGameplayFrame();
    ::Input::LateUpdate();

    // quit if requested
    if (quit_requested_this_frame) {
        is_game_running = false;
    }
}



/*
renders a frozen preview frame:
no gameplay update (component updates, physics step, or any frame advance).
except  deferred scene load / OnStart work so the preview reflects.
(we consider them to be logically last frames' behaviors)
*/
void Engine::RunRenderFrozenFrame(bool quit_requested_this_frame) {
    
    if (!IsRunning()) return;

    // Deferred runtime state still needs to become visible in the frozen
    // preview, even though simulation itself is paused.
    processPendingSceneLoad();
    ComponentManager::ApplyEffectiveRigidbodyBodyTypes();
    ComponentManager::ProcessPendingOnStart();
    render();
    ::Input::LateUpdate();

    if (quit_requested_this_frame) {
        is_game_running = false;
    }
}

/*
Pause-mode path:
keep the last runtime render target exactly as it was on the previous gameplay
frame, and only clear the editor host backbuffer so ImGui can compose over it.
*/
void Engine::RunPresentPausedFrame(bool quit_requested_this_frame) {
    if (!IsRunning()) return;

    if (render_runtime_to_texture_) {
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_RenderSetViewport(renderer, nullptr);
        clearEditorHostFrame();
    }

    ::Input::LateUpdate();
    if (quit_requested_this_frame) {
        is_game_running = false;
    }
}

/*
Notice: this is the input processor for runtime.
The input processor for editor is in EditorApp::Run(), with a single overlay_.ProcessEvent()
But: both standalone runtime and editor embedding feed the same input path
*/
void Engine::ProcessSDLEvent(const SDL_Event &event,
                             bool &quit_requested_this_frame) {
    ::Input::ProcessEvent(event);
    if (event.type == SDL_QUIT) {
        quit_requested_this_frame = true;
    }
}

// Present the current backbuffer to the window once drawing is complete.
void Engine::PresentFrame(bool advance_gameplay_frame) {
    if (renderer == nullptr) return;
    SDLRenderHelper::SDL_RenderPresent(renderer, advance_gameplay_frame);
    RefreshPerformanceCounters();
}

// Report whether the runtime loop is still active.
bool Engine::IsRunning() const {return is_game_running;}
// Ask the runtime loop to stop after the current frame.
void Engine::RequestQuit() {is_game_running = false;}
SDL_Window *Engine::GetWindow() const {return window;}
SDL_Renderer *Engine::GetRenderer() const {return renderer;}


int Engine::GetWindowWidth() const {
    if (window == nullptr) return config_.window_width;
    int width = config_.window_width;
    int height = config_.window_height;
    SDL_GetWindowSize(window, &width, &height);
    return width;
}
int Engine::GetWindowHeight() const {
    if (window == nullptr) return config_.window_height;
    int width = config_.window_width;
    int height = config_.window_height;
    SDL_GetWindowSize(window, &width, &height);
    return height;
}

const GameConfigData &Engine::GetConfig() const {return config_;}
std::size_t Engine::GetActorCount() const {return actors.size();}
const std::deque<Actor> &Engine::GetRuntimeActors() const {return actors;}
const Actor *Engine::GetRuntimeActorByID(int actor_id) const {
    for (const Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        if (actor.id != actor_id) continue;
        return &actor;
    }
    return nullptr;
}
const Actor *Engine::GetRuntimeActorByEditorUID(std::uint64_t actor_uid) const {
    return findRuntimeActorByEditorUID(actor_uid);
}

PhysicsHierarchy::State Engine::GetRuntimePhysicsHierarchyStateByID(int actor_id) const {
    const Actor *actor = GetRuntimeActorByID(actor_id);
    if (actor == nullptr) return {};
    return GetRuntimePhysicsHierarchyStateByEditorUID(actor->editor_actor_uid);
}

PhysicsHierarchy::State Engine::GetRuntimePhysicsHierarchyStateByEditorUID(std::uint64_t actor_uid) const {
    if (actor_uid == PhysicsHierarchy::kInvalidActorUID) return {};
    rebuildRuntimePhysicsHierarchyCache();
    auto state_it = runtime_physics_hierarchy_state_by_uid_.find(actor_uid);
    if (state_it == runtime_physics_hierarchy_state_by_uid_.end()) return {};
    return state_it->second;
}

std::uint64_t Engine::AllocateRuntimeGeneratedActorUID() {
    if (next_runtime_generated_actor_uid_ <
        Actor::kRuntimeGeneratedEditorActorUIDStart) {
        next_runtime_generated_actor_uid_ =
            Actor::kRuntimeGeneratedEditorActorUIDStart;
    }
    return next_runtime_generated_actor_uid_++;
}

bool Engine::DuplicateRuntimeActorByID(int actor_id, int *out_new_actor_id) {
    const Actor *source_actor = GetRuntimeActorByID(actor_id);
    if (source_actor == nullptr) return false;

    Actor duplicated_actor = *source_actor;
    const std::string duplicate_base_name =
        source_actor->actor_name.empty() ? "New Actor"
                                         : source_actor->actor_name + " (copy)";
    duplicated_actor.actor_name =
        BuildUniqueRuntimeActorName(actors, duplicate_base_name);
    duplicated_actor.id = Scene::AllocateActorID();
    duplicated_actor.editor_actor_uid = AllocateRuntimeGeneratedActorUID();
    duplicated_actor.scene_backed = false;
    duplicated_actor.parent_editor_actor_uid = Actor::kInvalidEditorActorUID;
    duplicated_actor.parent_id = -1;
    duplicated_actor.runtime_destroyed = false;

    actors.emplace_back(std::move(duplicated_actor));
    Actor *runtime_actor = &actors.back();

    ComponentManager::BindActorsForScene(actors);
    for (const Actor::ComponentSpec &component_spec :
         runtime_actor->component_specs) {
        ComponentManager::InstantiateComponentForActor(runtime_actor->id,
                                                       component_spec);
    }
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();

    if (out_new_actor_id != nullptr) {
        *out_new_actor_id = runtime_actor->id;
    }
    return true;
}
bool Engine::DeleteRuntimeActorByID(int actor_id) {
    for (Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        if (actor.id != actor_id) continue;

        ComponentManager::DestroyActor(&actor);
        ComponentManager::FinalizeFrameMutations();
        ComponentManager::BindActorsForScene(actors);
        rebuildRuntimeActorUIDMap();
        return true;
    }
    return false;
}

bool Engine::SetRuntimeActorParentByID(int actor_id, int parent_actor_id) {
    // Runtime-generated actors are appended directly by the scripting layer, so
    // the UID cache may lag behind until we explicitly resync it here.
    rebuildRuntimeActorUIDMap();

    const Actor *actor = GetRuntimeActorByID(actor_id);
    if (actor == nullptr) return false;
    if (actor->editor_actor_uid == Actor::kInvalidEditorActorUID) return false;

    SceneFormat::SetActorParentMutation mutation;
    mutation.actor_uid = actor->editor_actor_uid;

    if (parent_actor_id >= 0) {
        const Actor *parent_actor = GetRuntimeActorByID(parent_actor_id);
        if (parent_actor == nullptr) return false;
        if (parent_actor->editor_actor_uid == Actor::kInvalidEditorActorUID) {
            return false;
        }
        mutation.parent_actor_uid = parent_actor->editor_actor_uid;
    }

    return applySetActorParentMutation(mutation);
}

/*
Reload the runtime from a shared scene asset snapshot. (instead of re-reading from disk)

Notice: Play/Stop transitions can leave transient input/audio state from the
previous runtime snapshot, so clear those caches before rebind.
*/
void Engine::LoadSceneAsset(const SceneFormat::SceneAsset &scene_asset) {

    ::Input::Init();
    AudioManager::HaltChannel(-1);
    SDL_FlushEvent(SDL_KEYDOWN);
    SDL_FlushEvent(SDL_KEYUP);
    SDL_FlushEvent(SDL_TEXTEDITING);
    SDL_FlushEvent(SDL_TEXTINPUT);
    SDL_FlushEvent(SDL_MOUSEMOTION);
    SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);
    SDL_FlushEvent(SDL_MOUSEBUTTONUP);
    SDL_FlushEvent(SDL_MOUSEWHEEL);

    ComponentManager::ClearActorComponents();
    actors.clear();

    Scene::SetActiveSceneSubdirectory(scene_asset.scene_subdirectory);
    ComponentManager::ReloadComponentTypes();

    std::vector<Actor> runtime_actors = SceneFormat::BuildRuntimeActors(scene_asset);
    for (Actor &actor : runtime_actors) {
        actor.id = Scene::AllocateActorID();
        actors.emplace_back(std::move(actor));
    }

    ComponentManager::BindActorsForScene(actors);
    for (const Actor &actor : actors) {
        for (const Actor::ComponentSpec &component_spec : actor.component_specs) {
            ComponentManager::InstantiateComponentForActor(actor.id, component_spec);
        }
    }
    // Component instances are created after the deque is stable. Rebind once so
    // actor pointers and cached lifecycle lists both see the fresh components.
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();

    current_scene_name = scene_asset.scene_name;
    has_pending_scene_load = false;
    pending_scene_name.clear();
}

/*
load a scene edit command from the editor and apply it to the live runtime world.
this is API for the editor to manipulate the runtime world (atomically), 
can be used for hot reloading.
(兼容 hot reloading, 把一个 editor-side 的 scene edit command 加载到 runtime)

It applys a batch of scene mutations,
if any single mutation fails to apply then the whole command is rejected and rolled back.
(apply 一系列的 scene mutations, 如果其中一个无法 apply 就拒绝整个 command 并 rollback)
*/
bool Engine::ApplySceneEditCommand(const SceneFormat::SceneEditCommand &command) {
    if (command.empty()) return false;

    // fail fast on the first mutation that cannot be mirrored into the runtime
    for (const SceneFormat::SceneMutation &mutation : command.mutations) {
        if (!applySceneMutation(mutation)) {
            return false;
        }
    }
    return true;
}

// Toggle offscreen runtime rendering for the docked editor viewport.
void Engine::SetRenderRuntimeToTexture(bool enabled) {
    render_runtime_to_texture_ = enabled;
    if (!enabled) {
        destroyRuntimeRenderTarget();
        return;
    }
    ensureRuntimeRenderTarget();
}

// Return the latest runtime render target for ImGui::Image().
SDL_Texture *Engine::GetRuntimeRenderTarget() const {return runtime_render_target_;}
int Engine::GetRuntimeRenderTargetWidth() const {return runtime_render_target_width_;}
int Engine::GetRuntimeRenderTargetHeight() const {return runtime_render_target_height_;}
SDL_Texture *Engine::RenderScenePreview(float camera_x, float camera_y,
                                        float zoom_factor, int preview_width,
                                        int preview_height,
                                        bool scene_backed_only) {
    if (renderer == nullptr) return nullptr;

    ensureScenePreviewRenderTarget(preview_width, preview_height);
    if (scene_preview_render_target_ == nullptr) return nullptr;

    SDL_SetRenderTarget(renderer, scene_preview_render_target_);
    SDL_RenderSetViewport(renderer, nullptr);
    clearFrame();

    // Rebuild draw requests for an editor-only scene preview pass. This keeps
    // the scene camera independent without disturbing the live runtime view.
    ComponentManager::ResolveTransformHierarchy();
    ComponentManager::QueueBuiltinRenderers(scene_backed_only);
    ParticleManager::QueueRenderBatches();

    const float clamped_zoom =
        std::clamp(zoom_factor, kMinZoomFactor, kMaxZoomFactor);
    Renderer::RenderFrame(renderer, camera_x, camera_y, clamped_zoom,
                          scene_preview_render_target_width_,
                          scene_preview_render_target_height_);

    SDL_SetRenderTarget(renderer, nullptr);
    SDL_RenderSetViewport(renderer, nullptr);
    return scene_preview_render_target_;
}
int Engine::GetScenePreviewRenderTargetWidth() const {
    return scene_preview_render_target_width_;
}
int Engine::GetScenePreviewRenderTargetHeight() const {
    return scene_preview_render_target_height_;
}
float Engine::GetRuntimeRenderFPS() const {return displayed_runtime_render_fps_;}
float Engine::GetGameplayFPS() const {return displayed_gameplay_fps_;}


/*
SDL objects are created lazily so the same Engine can exist before the
runtime window/renderer are needed by either the game or the editor host.
*/
void Engine::initialize() {
    FrameClock::Reset();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) exit(0);

    // created on demand
    window = SDLRenderHelper::SDL_CreateWindow(
        config_.game_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        config_.window_width, config_.window_height, SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        SDL_Quit();
        exit(0);
    }

    renderer = SDLRenderHelper::SDL_CreateRenderer( window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        exit(0);
    }

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        exit(0);
    }

    Renderer::Init();
    ::Input::Init();

    runtime_zoom_factor = (config_.zoom_factor > 0.0f)
                              ? std::clamp(config_.zoom_factor, kMinZoomFactor,
                                           kMaxZoomFactor)
                              : 1.0f;

    clearFrame();
    ResetPerformanceCounters();
}

void Engine::update() {
    // gameplay update: now fully component-driven in Lua
    ComponentManager::ProcessOnUpdate();
    ComponentManager::ProcessOnLateUpdate();

    // Only pre-physics destruction/removal work is flushed here. Other
    // end-of-frame mutations still wait until after the Box2D step.
    ComponentManager::FinalizePrePhysicsDestructions();

    ComponentManager::StepPhysics();
    ParticleManager::Update(1.0f / 60.0f);

    // Full end-of-frame reconciliation happens once, after physics/callbacks.
    ComponentManager::FinalizeFrameMutations();
}


/*
Scene changes requested from gameplay are deferred to the next frame
boundary so actor destruction/creation never races the current update.
(gameplay 请求的场景切换被延迟到下一帧边界, 这样 actor 销毁/创建就永远不会和当前 update 竞争)
*/
void Engine::processPendingSceneLoad() {
    if (!has_pending_scene_load) return;

    const std::string scene_to_load = pending_scene_name;
    has_pending_scene_load = false;
    pending_scene_name.clear();

    // Unload current scene actors, except ones marked DontDestroy.
    for (Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        if (actor.dont_destroy_on_scene_load) continue;
        ComponentManager::DestroyActor(&actor);
    }
    // Apply removals immediately before loading new scene.
    ComponentManager::FinalizeFrameMutations();

    // Load new scene actors and append them to runtime container.
    std::vector<Actor> loaded_actors = Scene::LoadScene(scene_to_load);
    for (Actor &actor : loaded_actors) {
        actors.emplace_back(std::move(actor));
    }
    // Rebuild actor indices and re-inject actor refs into component tables.
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();
    current_scene_name = scene_to_load;
}

void Engine::SetCameraPosition(float x, float y) {camera_position.x = x; camera_position.y = y;}
float Engine::GetCameraPositionX() const {return camera_position.x;}
float Engine::GetCameraPositionY() const {return camera_position.y;}
void Engine::SetCameraZoom(float zoom_factor) {runtime_zoom_factor = zoom_factor;}
float Engine::GetCameraZoom() const {return runtime_zoom_factor;}


void Engine::RequestSceneLoad(const std::string &scene_name) {
    has_pending_scene_load = true;
    pending_scene_name = scene_name;
}
std::string Engine::GetCurrentSceneName() const {return current_scene_name;}

void Engine::ResetPerformanceCounters() {
    performance_sample_start_ticks_ = SDL_GetTicks();
    performance_runtime_render_frames_ = 0;
    performance_gameplay_frames_ = 0;
    displayed_runtime_render_fps_ = 0.0f;
    displayed_gameplay_fps_ = 0.0f;
}

void Engine::RecordRuntimeRenderFrame() {
    ++performance_runtime_render_frames_;
}

void Engine::RecordGameplayFrame() {
    ++performance_gameplay_frames_;
}

void Engine::RefreshPerformanceCounters() {
    if (performance_sample_start_ticks_ == 0) {
        performance_sample_start_ticks_ = SDL_GetTicks();
        return;
    }

    const std::uint32_t now = SDL_GetTicks();
    const std::uint32_t elapsed_ticks =
        now - performance_sample_start_ticks_;
    if (elapsed_ticks < 1000) return;

    const float elapsed_seconds =
        static_cast<float>(elapsed_ticks) / 1000.0f;
    displayed_runtime_render_fps_ =
        static_cast<float>(performance_runtime_render_frames_) /
        elapsed_seconds;
    displayed_gameplay_fps_ =
        static_cast<float>(performance_gameplay_frames_) / elapsed_seconds;

    performance_sample_start_ticks_ = now;
    performance_runtime_render_frames_ = 0;
    performance_gameplay_frames_ = 0;
}

/*
This flag is consumed by processPendingSceneLoad() to preserve actors
across runtime-initiated scene switches.
*/
void Engine::MarkActorDontDestroy(Actor *actor) {
    if (actor == nullptr) return;
    actor->dont_destroy_on_scene_load = true;
}

void Engine::invalidateRuntimePhysicsHierarchyCache() {
    runtime_physics_hierarchy_cache_dirty_ = true;
}

void Engine::rebuildRuntimePhysicsHierarchyCache() const {
    if (!runtime_physics_hierarchy_cache_dirty_) return;

    runtime_physics_hierarchy_state_by_uid_.clear();
    runtime_physics_hierarchy_state_by_uid_.reserve(actors.size());

    std::unordered_map<int, const Actor *> runtime_actor_by_id;
    runtime_actor_by_id.reserve(actors.size());
    for (const Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        runtime_actor_by_id[actor.id] = &actor;
    }

    std::unordered_map<int, PhysicsHierarchy::State> state_by_actor_id;
    state_by_actor_id.reserve(actors.size());
    std::unordered_set<int> resolved_actor_ids;
    std::unordered_set<int> resolving_actor_ids;

    std::function<void(const Actor &)> resolve_actor_state =
        [&](const Actor &actor) {
            if (actor.runtime_destroyed) return;
            if (resolved_actor_ids.find(actor.id) != resolved_actor_ids.end()) {
                return;
            }
            if (!resolving_actor_ids.insert(actor.id).second) {
                return;
            }

            PhysicsHierarchy::State state = BuildRuntimeActorPhysicsSelfState(
                actor.id, ComponentManager::GetRuntimeComponentSpecs(actor.id));

            std::uint64_t nearest_dynamic_ancestor_uid =
                PhysicsHierarchy::kInvalidActorUID;
            if (actor.parent_id >= 0 && actor.parent_id != actor.id) {
                auto parent_it = runtime_actor_by_id.find(actor.parent_id);
                if (parent_it != runtime_actor_by_id.end() &&
                    parent_it->second != nullptr) {
                    resolve_actor_state(*parent_it->second);
                    const PhysicsHierarchy::State &parent_state =
                        state_by_actor_id[parent_it->second->id];
                    if (parent_state.has_dynamic_rigidbody_self) {
                        nearest_dynamic_ancestor_uid =
                            parent_it->second->editor_actor_uid;
                    } else {
                        nearest_dynamic_ancestor_uid =
                            parent_state.nearest_dynamic_body_ancestor_uid;
                    }
                }
            }

            state.nearest_dynamic_body_ancestor_uid =
                nearest_dynamic_ancestor_uid;
            state.is_under_dynamic_hierarchy =
                nearest_dynamic_ancestor_uid != PhysicsHierarchy::kInvalidActorUID;
            if (state.has_dynamic_rigidbody_self) {
                state.physics_root_uid = actor.editor_actor_uid;
            } else if (state.is_under_dynamic_hierarchy) {
                state.physics_root_uid = nearest_dynamic_ancestor_uid;
            }
            if (state.rigidbody_enabled_self &&
                state.requested_body_type == "dynamic" &&
                state.is_under_dynamic_hierarchy) {
                state.effective_body_type = "kinematic";
            }

            state_by_actor_id[actor.id] = state;
            if (actor.editor_actor_uid != PhysicsHierarchy::kInvalidActorUID) {
                runtime_physics_hierarchy_state_by_uid_[actor.editor_actor_uid] =
                    state;
            }

            resolving_actor_ids.erase(actor.id);
            resolved_actor_ids.insert(actor.id);
        };

    for (const Actor &actor : actors) {
        resolve_actor_state(actor);
    }

    runtime_physics_hierarchy_cache_dirty_ = false;
}

/*
Editor-authored actor UIDs stay stable across runtime rebuilds. This map
lets live edit commands find the current runtime actor after reloads.
*/
void Engine::rebuildRuntimeActorUIDMap() {
    invalidateRuntimePhysicsHierarchyCache();
    runtime_actor_by_editor_uid_.clear();
    runtime_actor_by_editor_uid_.reserve(actors.size());
    std::uint64_t max_existing_uid = 0;
    for (Actor &actor : actors) {
        if (actor.runtime_destroyed) continue;
        if (actor.editor_actor_uid == SceneFormat::kInvalidSceneActorUID) continue;
        max_existing_uid = std::max(max_existing_uid, actor.editor_actor_uid);
        runtime_actor_by_editor_uid_[actor.editor_actor_uid] = &actor;
    }
    next_runtime_generated_actor_uid_ =
        std::max<std::uint64_t>(Actor::kRuntimeGeneratedEditorActorUIDStart,
                                max_existing_uid + 1);
    rebuildRuntimeParentLinks();
}

void Engine::rebuildRuntimeParentLinks() {
    // Runtime parent links are derived from the persisted stable actor UIDs, so
    // editor-authored hierarchy can survive scene reloads and hot mirroring.
    for (Actor &actor : actors) {
        actor.parent_id = -1;
        if (actor.runtime_destroyed) continue;
        if (actor.parent_editor_actor_uid == SceneFormat::kInvalidSceneActorUID) {
            continue;
        }

        const Actor *parent_actor =
            findRuntimeActorByEditorUID(actor.parent_editor_actor_uid);
        if (parent_actor == nullptr) continue;
        if (parent_actor->id == actor.id) continue;
        actor.parent_id = parent_actor->id;
    }
}

Actor* Engine::findRuntimeActorByEditorUID(std::uint64_t actor_uid) {
    auto actor_it = runtime_actor_by_editor_uid_.find(actor_uid);
    if (actor_it == runtime_actor_by_editor_uid_.end()) return nullptr;
    if (actor_it->second == nullptr || actor_it->second->runtime_destroyed) {
        return nullptr;
    }
    return actor_it->second;
}
const Actor* Engine::findRuntimeActorByEditorUID(std::uint64_t actor_uid) const {
    auto actor_it = runtime_actor_by_editor_uid_.find(actor_uid);
    if (actor_it == runtime_actor_by_editor_uid_.end()) return nullptr;
    if (actor_it->second == nullptr || actor_it->second->runtime_destroyed) {
        return nullptr;
    }
    return actor_it->second;
}

/*
applySceneMutation: variant visitor, 
apply one scene mutation to the runtime world. 
*/
bool Engine::applySceneMutation(const SceneFormat::SceneMutation &mutation) {
    // Scene mutations are variant-dispatched here so the editor can stream one
    // serialized command format into the live runtime mirror.
    return std::visit(
        [&](const auto &typed_mutation) -> bool {
            using MutationType = std::decay_t<decltype(typed_mutation)>;
            if constexpr (std::is_same_v<MutationType,
                                         SceneFormat::CreateActorMutation>) {
                return applyCreateActorMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::DeleteActorMutation>) {
                return applyDeleteActorMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::SetActorNameMutation>) {
                return applySetActorNameMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::SetActorParentMutation>) {
                return applySetActorParentMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::AddComponentMutation>) {
                return applyAddComponentMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::DeleteComponentMutation>) {
                return applyDeleteComponentMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::RenameComponentMutation>) {
                return applyRenameComponentMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::SetComponentTypeMutation>) {
                return applySetComponentTypeMutation(typed_mutation);
            } else if constexpr (std::is_same_v<
                                     MutationType,
                                     SceneFormat::SetComponentPropertyMutation>) {
                return applySetComponentPropertyMutation(typed_mutation);
            }
            return false;
        },
        mutation.payload);
}

bool Engine::applyCreateActorMutation( const SceneFormat::CreateActorMutation &mutation) {
    // Create the effective runtime actor from editor data, insert it at the
    // requested hierarchy position, then instantiate all component instances.
    if (mutation.actor_uid == SceneFormat::kInvalidSceneActorUID) return false;
    if (findRuntimeActorByEditorUID(mutation.actor_uid) != nullptr) return false;

    Actor actor = SceneFormat::BuildEffectiveActor(
        mutation.actor_record, Scene::GetActiveSceneSubdirectory());
    actor.id = Scene::AllocateActorID();
    actor.editor_actor_uid = mutation.actor_uid;

    auto insert_it = actors.end();
    if (mutation.insert_after_actor_uid.has_value()) {
        insert_it = std::find_if(
            actors.begin(), actors.end(), [&](const Actor &existing_actor) {
                return !existing_actor.runtime_destroyed &&
                       existing_actor.editor_actor_uid ==
                           *mutation.insert_after_actor_uid;
            });
        if (insert_it == actors.end()) return false;
        ++insert_it;
    }

    actors.insert(insert_it, std::move(actor));
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();

    Actor *runtime_actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (runtime_actor == nullptr) return false;
    for (const Actor::ComponentSpec &component_spec :
         runtime_actor->component_specs) {
        ComponentManager::InstantiateComponentForActor(runtime_actor->id,
                                                       component_spec);
    }
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();
    return true;
}

bool Engine::applyDeleteActorMutation( const SceneFormat::DeleteActorMutation &mutation) {
    // Deletion is two-phase in the component system, so finalize immediately
    // before rebuilding lookup tables.
    Actor *actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (actor == nullptr) return false;

    ComponentManager::DestroyActor(actor);
    ComponentManager::FinalizeFrameMutations();
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();
    return true;
}

bool Engine::applySetActorNameMutation( const SceneFormat::SetActorNameMutation &mutation) {
    // Renaming is structurally simple, but we still rebind so cached actor
    // references in subsystems see a consistent container state.
    Actor *actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (actor == nullptr) return false;
    if (mutation.actor_name.empty()) return false;
    if (actor->actor_name == mutation.actor_name) return false;

    actor->actor_name = mutation.actor_name;
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();
    return true;
}

bool Engine::applySetActorParentMutation( const SceneFormat::SetActorParentMutation &mutation) {
    Actor *actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (actor == nullptr) return false;

    Actor *parent_actor = nullptr;
    if (mutation.parent_actor_uid.has_value()) {
        parent_actor = findRuntimeActorByEditorUID(*mutation.parent_actor_uid);
        if (parent_actor == nullptr) return false;
        if (parent_actor->id == actor->id) return false;

        std::unordered_set<int> visited_actor_ids;
        const Actor *ancestor_actor = parent_actor;
        while (ancestor_actor != nullptr) {
            if (ancestor_actor->id == actor->id) return false;
            if (!visited_actor_ids.insert(ancestor_actor->id).second) {
                return false;
            }
            if (ancestor_actor->parent_editor_actor_uid ==
                SceneFormat::kInvalidSceneActorUID) {
                break;
            }
            ancestor_actor = findRuntimeActorByEditorUID( ancestor_actor->parent_editor_actor_uid);
        }
    }

    const std::uint64_t current_parent_uid = actor->parent_editor_actor_uid;
    const std::uint64_t new_parent_uid =
        mutation.parent_actor_uid.value_or(SceneFormat::kInvalidSceneActorUID);
    if (current_parent_uid == new_parent_uid) return false;

    float actor_world_x = 0.0f;
    float actor_world_y = 0.0f;
    float actor_world_rotation = 0.0f;
    std::string transform_component_key;
    const bool has_transform = ComponentManager::TryGetRuntimeTransformWorld(
        actor->id, actor_world_x, actor_world_y, actor_world_rotation,
        &transform_component_key);

    std::optional<float> parent_world_x;
    std::optional<float> parent_world_y;
    std::optional<float> parent_world_rotation;
    if (has_transform && parent_actor != nullptr) {
        float resolved_parent_world_x = 0.0f;
        float resolved_parent_world_y = 0.0f;
        float resolved_parent_world_rotation = 0.0f;
        if (ComponentManager::TryGetRuntimeTransformWorld(
                parent_actor->id, resolved_parent_world_x,
                resolved_parent_world_y, resolved_parent_world_rotation,
                nullptr)) {
            parent_world_x = resolved_parent_world_x;
            parent_world_y = resolved_parent_world_y;
            parent_world_rotation = resolved_parent_world_rotation;
        }
    }

    actor->parent_editor_actor_uid = new_parent_uid;
    invalidateRuntimePhysicsHierarchyCache();
    rebuildRuntimeParentLinks();

    if (has_transform) {
        float local_x = actor_world_x;
        float local_y = actor_world_y;
        float local_rotation = actor_world_rotation;
        if (parent_world_x.has_value() && parent_world_y.has_value() &&
            parent_world_rotation.has_value()) {
            const float relative_world_x = actor_world_x - *parent_world_x;
            const float relative_world_y = actor_world_y - *parent_world_y;
            RotateClockwise(relative_world_x, relative_world_y,
                            -*parent_world_rotation, local_x, local_y);
            local_rotation = actor_world_rotation - *parent_world_rotation;
        }

        ComponentManager::SetRuntimeComponentPropertyValue(
            actor->id, transform_component_key, "x",
            static_cast<double>(local_x));
        ComponentManager::SetRuntimeComponentPropertyValue(
            actor->id, transform_component_key, "y",
            static_cast<double>(local_y));
        ComponentManager::SetRuntimeComponentPropertyValue(
            actor->id, transform_component_key, "rotation",
            static_cast<double>(local_rotation));
    }

    return true;
}

bool Engine::applyAddComponentMutation( const SceneFormat::AddComponentMutation &mutation) {
    // Component specs are the authoritative serialized form; append the spec,
    // instantiate the live component, then refresh actor/component bindings.
    Actor *actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (actor == nullptr) return false;
    if (mutation.component_spec.key.empty() || mutation.component_spec.type.empty()) {
        return false;
    }
    if (SceneFormat::FindComponentSpec(actor->component_specs,
                                       mutation.component_spec.key) != nullptr) {
        return false;
    }

    actor->component_specs.emplace_back(mutation.component_spec);
    SceneFormat::SortComponentSpecs(actor->component_specs);
    ComponentManager::InstantiateComponentForActor(actor->id,
                                                   mutation.component_spec);
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();
    return true;
}

bool Engine::applyDeleteComponentMutation( const SceneFormat::DeleteComponentMutation &mutation) {
    // Remove from serialized specs and live runtime together so the mirrored
    // world stays aligned with what the editor believes exists.
    Actor *actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (actor == nullptr) return false;

    const Actor::ComponentSpec *component_spec =
        SceneFormat::FindComponentSpec(actor->component_specs,
                                       mutation.component_key);
    if (component_spec == nullptr) return false;

    const luabridge::LuaRef component_ref =
        ComponentManager::GetComponentByKey(actor->id, mutation.component_key);
    if (component_ref.isNil()) return false;

    actor->component_specs.erase(
        std::remove_if(actor->component_specs.begin(), actor->component_specs.end(),
                       [&](const Actor::ComponentSpec &component_spec_to_remove) {
                           return component_spec_to_remove.key ==
                                  mutation.component_key;
                       }),
        actor->component_specs.end());
    ComponentManager::RemoveComponent(actor->id, component_ref);
    ComponentManager::FinalizeFrameMutations();
    ComponentManager::BindActorsForScene(actors);
    rebuildRuntimeActorUIDMap();
    return true;
}

bool Engine::applyRenameComponentMutation( const SceneFormat::RenameComponentMutation &mutation) {
    // Renaming updates both the serialized component key and the live component
    // registry entry used by scripting lookups.
    Actor *actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (actor == nullptr) return false;
    if (mutation.component_key.empty() || mutation.new_component_key.empty()) {
        return false;
    }
    if (SceneFormat::FindComponentSpec(actor->component_specs,
                                       mutation.new_component_key) != nullptr) {
        return false;
    }

    Actor::ComponentSpec *component_spec =
        SceneFormat::FindComponentSpec(actor->component_specs,
                                       mutation.component_key);
    if (component_spec == nullptr) return false;
    component_spec->key = mutation.new_component_key;
    SceneFormat::SortComponentSpecs(actor->component_specs);
    if (!ComponentManager::RenameComponentKey(actor->id, mutation.component_key,
                                              mutation.new_component_key)) {
        return false;
    }
    return true;
}

bool Engine::applySetComponentTypeMutation( const SceneFormat::SetComponentTypeMutation &mutation) {
    // Changing type is effectively a remove+recreate operation because the old
    // instance layout/script binding is no longer valid.
    Actor *actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (actor == nullptr) return false;
    Actor::ComponentSpec *component_spec =
        SceneFormat::FindComponentSpec(actor->component_specs,
                                       mutation.component_key);
    if (component_spec == nullptr) return false;
    if (component_spec->type == mutation.type_name) return false;

    const luabridge::LuaRef component_ref =
        ComponentManager::GetComponentByKey(actor->id, mutation.component_key);
    if (component_ref.isNil()) return false;

    ComponentManager::RemoveComponent(actor->id, component_ref);
    ComponentManager::FinalizeFrameMutations();
    component_spec = SceneFormat::FindComponentSpec(actor->component_specs,
                                                    mutation.component_key);
    if (component_spec == nullptr) return false;
    component_spec->type = mutation.type_name;
    if (!rebuildRuntimeComponentFromSpec(*actor, mutation.component_key)) {
        return false;
    }
    rebuildRuntimeActorUIDMap();
    return true;
}

bool Engine::applySetComponentPropertyMutation( const SceneFormat::SetComponentPropertyMutation &mutation) {
    // Prefer in-place property patching for generic script components. Builtin
    // components fall back to full reconstruction so native state stays synced.
    Actor *actor = findRuntimeActorByEditorUID(mutation.actor_uid);
    if (actor == nullptr) return false;
    Actor::ComponentSpec *component_spec =
        SceneFormat::FindComponentSpec(actor->component_specs,
                                       mutation.component_key);
    if (component_spec == nullptr) return false;
    SceneFormat::UpsertComponentProperty(*component_spec, mutation.property_name,
                                         mutation.value);
    if (component_spec->type == "Rigidbody" &&
        (mutation.property_name == "enabled" ||
         mutation.property_name == "body_type")) {
        invalidateRuntimePhysicsHierarchyCache();
    }
    if ((!IsBuiltinRuntimeComponentType(component_spec->type) ||
         CanBuiltinRuntimeComponentPatchInPlace(component_spec->type)) &&
        ComponentManager::SetComponentPropertyValue(
            actor->id, mutation.component_key, mutation.property_name,
            mutation.value)) {
        return true;
    }

    const luabridge::LuaRef component_ref =
        ComponentManager::GetComponentByKey(actor->id, mutation.component_key);
    if (component_ref.isNil()) return false;

    ComponentManager::RemoveComponent(actor->id, component_ref);
    ComponentManager::FinalizeFrameMutations();
    if (!rebuildRuntimeComponentFromSpec(*actor, mutation.component_key)) {
        return false;
    }
    rebuildRuntimeActorUIDMap();
    return true;
}

/*
Rehydrate one live runtime component from the actor's serialized spec
after a destructive edit such as type/property replacement.
*/
bool Engine::rebuildRuntimeComponentFromSpec(Actor &actor,
                                             const std::string &component_key) {
    const Actor::ComponentSpec *component_spec =
        SceneFormat::FindComponentSpec(actor.component_specs, component_key);
    if (component_spec == nullptr) return false;

    ComponentManager::InstantiateComponentForActor(actor.id, *component_spec);
    ComponentManager::BindActorsForScene(actors);
    return true;
}

/*
The editor-owned embedded viewport is backed by this SDL texture. Destroy
it whenever embedding is disabled or the renderer/runtime is shutting down.
*/
void Engine::destroyRuntimeRenderTarget() {
    if (runtime_render_target_ != nullptr) {
        SDL_DestroyTexture(runtime_render_target_);
        runtime_render_target_ = nullptr;
    }
    runtime_render_target_width_ = 0;
    runtime_render_target_height_ = 0;
}

/*
Allocate or resize the offscreen texture that receives runtime rendering
before ImGui displays it inside the editor viewport.
*/
void Engine::ensureRuntimeRenderTarget() {
    if (!render_runtime_to_texture_) return;
    if (renderer == nullptr) return;

    if (runtime_render_target_ != nullptr &&
        runtime_render_target_width_ == config_.window_width &&
        runtime_render_target_height_ == config_.window_height) {
        return;
    }

    destroyRuntimeRenderTarget();
    runtime_render_target_ = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        config_.window_width, config_.window_height);
    if (runtime_render_target_ == nullptr) {
        render_runtime_to_texture_ = false;
        return;
    }

    SDL_SetTextureBlendMode(runtime_render_target_, SDL_BLENDMODE_BLEND);
    runtime_render_target_width_ = config_.window_width;
    runtime_render_target_height_ = config_.window_height;
}

/*
Scene panel owns a separate preview texture so its editor camera can diverge
from the live runtime camera without stretching/cropping the game viewport.
*/
void Engine::destroyScenePreviewRenderTarget() {
    if (scene_preview_render_target_ != nullptr) {
        SDL_DestroyTexture(scene_preview_render_target_);
        scene_preview_render_target_ = nullptr;
    }
    scene_preview_render_target_width_ = 0;
    scene_preview_render_target_height_ = 0;
}

void Engine::ensureScenePreviewRenderTarget(int width, int height) {
    if (renderer == nullptr) return;
    const int safe_width = std::max(1, width);
    const int safe_height = std::max(1, height);

    if (scene_preview_render_target_ != nullptr &&
        scene_preview_render_target_width_ == safe_width &&
        scene_preview_render_target_height_ == safe_height) {
        return;
    }

    destroyScenePreviewRenderTarget();
    scene_preview_render_target_ = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        safe_width, safe_height);
    if (scene_preview_render_target_ == nullptr) {
        return;
    }

    SDL_SetTextureBlendMode(scene_preview_render_target_, SDL_BLENDMODE_BLEND);
    scene_preview_render_target_width_ = safe_width;
    scene_preview_render_target_height_ = safe_height;
}
