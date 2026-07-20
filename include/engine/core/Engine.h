#ifndef ENGINE_H
#define ENGINE_H

#include "scene/Actor.h"
#include "shared/physics/PhysicsHierarchyState.h"
#include "shared/config/GameConfig.h"
#include "shared/scene_format/SceneFormat.h"
#include "shared/scene_format/SceneMutation.h"
#include "glm/glm.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
union SDL_Event;

class Engine
{
private:
    GameConfigData config_;

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;

    // runtime actor container for the current scene
    std::deque<Actor> actors;

    bool is_game_running = true;
    std::string current_scene_name = "";

    // queued scene load request, applied at the start of the next frame
    // 延迟到下一帧开头执行的场景切换请求
    bool has_pending_scene_load = false;
    std::string pending_scene_name = "";
    std::string last_scene_load_error_;

    // runtime camera state
    float runtime_zoom_factor = 1.0f;
    static constexpr float kMinZoomFactor = 0.1f;
    static constexpr float kMaxZoomFactor = 8.0f;
    glm::vec2 camera_position = glm::vec2(0.0f, 0.0f);

    // When the editor is active, runtime rendering is redirected into this
    // texture so Dear ImGui can present the game inside a docked viewport.
    bool render_runtime_to_texture_ = false;
    SDL_Texture *runtime_render_target_ = nullptr;
    int runtime_render_target_width_ = 0;
    int runtime_render_target_height_ = 0;
    int requested_runtime_render_target_width_ = 0;
    int requested_runtime_render_target_height_ = 0;
    SDL_Texture *scene_preview_render_target_ = nullptr;
    int scene_preview_render_target_width_ = 0;
    int scene_preview_render_target_height_ = 0;
    Actor::UID next_runtime_generated_actor_uid_ =
        Actor::kRuntimeGeneratedUIDStart;
    std::unordered_map<Actor::UID, Actor *> runtime_actor_by_uid_;
    mutable bool runtime_physics_hierarchy_cache_dirty_ = true;
    mutable std::unordered_map<std::uint64_t, PhysicsHierarchy::State>
        runtime_physics_hierarchy_state_by_uid_;
    // Sampled runtime counters let the editor compare UI cadence against
    // actual live runtime cadence when diagnosing visible stutter.
    std::uint32_t performance_sample_start_ticks_ = 0;
    int performance_runtime_render_frames_ = 0;
    int performance_gameplay_frames_ = 0;
    float displayed_runtime_render_fps_ = 0.0f;
    float displayed_gameplay_fps_ = 0.0f;

    void update();
    void render();
    void initialize();
    void processPendingSceneLoad();
    void clearFrame();
    void clearEditorHostFrame();
    void destroyRuntimeRenderTarget();
    void ensureRuntimeRenderTarget();
    void destroyScenePreviewRenderTarget();
    void ensureScenePreviewRenderTarget(int width, int height);
    void invalidateRuntimePhysicsHierarchyCache();
    void rebuildRuntimePhysicsHierarchyCache() const;
    void rebuildRuntimeActorUIDMap();
    Actor *findRuntimeActorByUID(Actor::UID actor_uid);
    const Actor *findRuntimeActorByUID(Actor::UID actor_uid) const;
    bool applySceneMutation(const SceneFormat::SceneMutation &mutation);
    bool applyCreateActorMutation( const SceneFormat::CreateActorMutation &mutation);
    bool applyDeleteActorMutation( const SceneFormat::DeleteActorMutation &mutation);
    bool applySetActorNameMutation( const SceneFormat::SetActorNameMutation &mutation);
    bool applySetActorParentMutation( const SceneFormat::SetActorParentMutation &mutation);
    bool applyAddComponentMutation( const SceneFormat::AddComponentMutation &mutation);
    bool applyDeleteComponentMutation( const SceneFormat::DeleteComponentMutation &mutation);
    bool applyRenameComponentMutation( const SceneFormat::RenameComponentMutation &mutation);
    bool applySetComponentTypeMutation( const SceneFormat::SetComponentTypeMutation &mutation);
    bool applySetComponentPropertyMutation( const SceneFormat::SetComponentPropertyMutation &mutation);
    bool rebuildRuntimeComponentFromSpec(Actor &actor,
                                         const std::string &component_key);
    void ResetPerformanceCounters();
    void RecordRuntimeRenderFrame();
    void RecordGameplayFrame();
    void RefreshPerformanceCounters();
    std::size_t CountLiveActors() const;
    void PruneDestroyedRuntimeActors();

public:
    Engine();
    ~Engine();
    // Start SDL/runtime systems without entering the blocking game loop.
    // 初始化 SDL/运行时系统, 但不进入游戏循环.
    void InitializeRuntime();
    // Tear down SDL/runtime systems created by InitializeRuntime.
    // 关闭由 InitializeRuntime 创建的 SDL/运行时系统.
    void ShutdownRuntime();
    // Feed one SDL event into the runtime input and quit handling path.
    // 将一个 SDL 事件传递给运行时输入和退出处理路径.
    void ProcessSDLEvent(const SDL_Event &event,
                         bool &quit_requested_this_frame);
    // Poll events internally and execute one runtime frame.
    // 内部轮询事件并执行一个运行时帧.
    void RunSingleFrame();
    void RunSingleFrame(bool quit_requested_this_frame);
    // Execute one frozen preview frame for editor edit mode.
    // It renders without advancing gameplay simulation. Editor live preview may
    // opt into flushing pending OnStart work for components in the mirror.
    void RunRenderFrozenFrame(bool quit_requested_this_frame,
                              bool process_pending_on_start);
    // Keep showing the last runtime texture while paused, without re-rendering
    // or mutating runtime state.
    void RunPresentPausedFrame(bool quit_requested_this_frame);
    // Present the current backbuffer to the window once drawing is complete.
    // `advance_gameplay_frame` should stay false for frozen editor previews.
    void PresentFrame(bool advance_gameplay_frame = true);
    // Run the traditional standalone runtime loop until quit.
    // 运行传统的独立运行时循环直到退出.
    void GameLoop();
    bool IsRunning() const;
    // Ask the runtime loop to stop after the current frame.
    // 请求运行时循环在当前帧结束后停止.
    void RequestQuit();
    // Expose the SDL window, renderer for editor/platform integration.
    SDL_Window *GetWindow() const;
    SDL_Renderer *GetRenderer() const;
    // Return the current host window width in screen pixels.
    int GetWindowWidth() const;
    // Return the current host window height in screen pixels.
    int GetWindowHeight() const;
    // Return the loaded game configuration data.
    const GameConfigData &GetConfig() const;
    const std::string &GetLastSceneLoadError() const;
    std::size_t GetActorCount() const;
    // Return read-only access to the current live runtime actor container.
    // Editors can inspect this during play mode without owning runtime state.
    const std::deque<Actor> &GetRuntimeActors() const;
    // Look up one live runtime actor by runtime id / stable editor uid.
    const Actor *GetRuntimeActorByUID(Actor::UID actor_uid) const;
    PhysicsHierarchy::State GetRuntimePhysicsHierarchyStateByUID(Actor::UID actor_uid) const;
    // Duplicate/delete one live runtime actor without touching on-disk scene files.
    bool DuplicateRuntimeActorByUID(Actor::UID actor_uid,
                                    Actor::UID *out_new_actor_uid = nullptr);
    bool DeleteRuntimeActorByUID(Actor::UID actor_uid);
    bool SetRuntimeActorParentByUID(Actor::UID actor_uid,
                                    Actor::UID parent_uid);
    // Runtime-only actors receive transient stable UIDs from a dedicated
    // allocator so they never depend on SceneDocument's authoring counter.
    Actor::UID AllocateRuntimeGeneratedActorUID();
    // Reload the runtime from a shared scene asset snapshot.
    // 使用共享场景资源快照重新装载运行时.
    bool LoadSceneAsset(const SceneFormat::SceneAsset &scene_asset);
    bool ApplySceneEditCommand(const SceneFormat::SceneEditCommand &command);
    // Toggle offscreen runtime rendering for the docked editor viewport.
    // 切换用于编辑器嵌入视口的离屏运行时渲染.
    void SetRenderRuntimeToTexture(bool enabled);
    // Request the editor runtime target size for the next runtime render pass.
    void SetRuntimeRenderTargetSize(int width, int height);
    // Return the latest runtime render target for ImGui::Image().
    // 返回给 ImGui::Image() 使用的最新运行时渲染目标.
    SDL_Texture *GetRuntimeRenderTarget() const;
    // Return the physical pixel size of the embedded runtime render target.
    int GetRuntimeRenderTargetWidth() const;
    int GetRuntimeRenderTargetHeight() const;
    // Render one editor-only scene preview using an independent camera.
    SDL_Texture *RenderScenePreview(float camera_x, float camera_y,
                                    float zoom_factor, int preview_width,
                                    int preview_height,
                                    bool scene_backed_only = false);
    int GetScenePreviewRenderTargetWidth() const;
    int GetScenePreviewRenderTargetHeight() const;
    // Return sampled runtime cadence metrics for editor diagnostics.
    float GetRuntimeRenderFPS() const;
    float GetGameplayFPS() const;
    void SetAudioPlaybackEnabled(bool enabled);
    bool IsAudioPlaybackEnabled() const;

    // camera API exposed to Lua
    void SetCameraPosition(float x, float y);
    float GetCameraPositionX() const;
    float GetCameraPositionY() const;
    void SetCameraZoom(float zoom_factor);
    float GetCameraZoom() const;

    // scene API exposed to Lua
    // Queue a scene change to be applied at the start of the next frame.
    void RequestSceneLoad(const std::string &scene_name);
    std::string GetCurrentSceneName() const;
    void MarkActorDontDestroy(Actor *actor);
};

#endif
