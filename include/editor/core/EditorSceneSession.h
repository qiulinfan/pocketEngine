#ifndef EDITOR_SCENE_SESSION_H
#define EDITOR_SCENE_SESSION_H

#include "editor/documents/SceneDocument.h"

class Engine;

/*
This class manages the editor-side scene document and its relationship to the
runtime mirror. It owns the editor cache for one .scene file, and tracks when 
the runtime mirror needs to be updated from editor edits. 

EditorApp coordinates the frame loop and delegates scene loading/saving/mirroring policy to this class, 
but does not directly own the document or dirty flags itself.

这个 class 管理 editor 端的 scene document 以及它和 runtime 镜像的关系. 
它持有一个 .scene 文件的 editor 缓存, 并跟踪 runtime 镜像何时需要从 editor 编辑中更新. 
EditorApp 协调帧循环并将场景加载/保存/镜像策略委托给这个 class, 但不直接拥有文档或 dirty 标志本身.
*/
class EditorSceneSession {
public:
    // Load the scene declared by game.config into the editor-owned cache.
    // 从 game.config 指定的场景加载 editor 持有的缓存.
    void LoadInitialScene(const Engine &engine);
    // Switch to another scene by .scene file path from the Project panel.
    bool OpenSceneFromPath(const std::filesystem::path &scene_path,
                           Engine &engine);
    // Report whether the editor currently has a scene document open.
    bool HasSceneDocument() const;
    // Return the current UI-facing scene document. In edit mode this is the
    // authoring document; in play mode this is the sandbox play copy.
    SceneDocument &GetSceneDocument();
    const SceneDocument &GetSceneDocument() const;
    // Return the authoritative editor-side scene cache that is eligible for
    // save-to-disk and edit-mode runtime mirroring.
    SceneDocument &GetAuthoringSceneDocument();
    const SceneDocument &GetAuthoringSceneDocument() const;
    bool HasPlaySceneDocument() const;
    // Mark the runtime mirror stale after the editor cache changed.
    // 当 editor 缓存变化后, 将 runtime 镜像标记为过期.
    void MarkRuntimeMirrorDirty();
    // Save the scene cache back to disk if editor edits made it dirty.
    void SaveDocumentIfDirty();
    // Rebuild runtime from the editor cache when the mirror is stale.
    // 当运行时镜像过期时, 用 editor 缓存重建 runtime.
    void SyncRuntimeMirrorIfDirty(Engine &engine);
    // Start/stop play mode. Play mode runs runtime from a snapshot copy so
    // editor-side scene cache remains stable for later restoration.
    // 启动/停止 Play 模式: 运行时使用快照副本, editor 侧缓存保持稳定.
    bool EnterPlayMode(Engine &engine);
    bool ExitPlayMode(Engine &engine);
    bool TogglePlayPause();
    void HandleSceneEditCommand(const SceneFormat::SceneEditCommand &command,
                                Engine &engine);
    void HandleSceneEditCommands(
        const std::vector<SceneFormat::SceneEditCommand> &commands,
        Engine &engine);
    // Called when the editor is closing: persist only editor document cache.
    // Runtime play-state mutations are discarded and never serialized.
    void PersistDocumentOnEditorShutdown();
    // Report current mode flags used by overlay and panel edit gates.
    bool IsPlayModeActive() const;
    bool IsPlayModePaused() const;
    bool IsSceneEditingEnabled() const;
    bool IsSceneSaveEnabled() const;

private:
    SceneDocument &GetActiveSceneDocument();
    const SceneDocument &GetActiveSceneDocument() const;

    // This session owns two editor-side documents:
    // 1) authoring document: canonical editor cache / save target
    // 2) play document: temporary sandbox copy used only during play mode
    SceneDocument authoring_scene_document_;
    SceneDocument play_scene_document_;
    bool authoring_scene_document_loaded_ = false;
    bool play_scene_document_loaded_ = false;
    bool runtime_scene_dirty_ = false;
    bool play_mode_active_ = false;
    bool play_mode_paused_ = false;
};

#endif
