#include "editor/core/EditorSceneSession.h"
#include "engine/core/Engine.h"
#include <utility>

// EditorSceneSession public API: scene loading / access
void EditorSceneSession::LoadInitialScene(const Engine &engine) {
    authoring_scene_document_ = SceneDocument();
    play_scene_document_ = SceneDocument();
    authoring_scene_document_loaded_ = false;
    play_scene_document_loaded_ = false;
    runtime_scene_dirty_ = false;
    play_mode_active_ = false;
    play_mode_paused_ = false;

    const std::string initial_scene_name = engine.GetConfig().initial_scene_name;
    if (initial_scene_name.empty()) {
        return;
    }

    authoring_scene_document_loaded_ = authoring_scene_document_.LoadFromSceneName(initial_scene_name);
    runtime_scene_dirty_ = authoring_scene_document_loaded_;
}

bool EditorSceneSession::OpenSceneFromPath( const std::filesystem::path &scene_path, Engine &engine) {
    if (scene_path.empty()) {
        return false;
    }
    /*
    Scene switch policy:
    1. save outgoing dirty document,
    2. hard-stop current play/edit session state,
    3. load next scene as a fresh editor document,
    4. immediately mirror it into runtime so no transitional leftovers remain.
    */
    SaveDocumentIfDirty();

    authoring_scene_document_ = SceneDocument();
    play_scene_document_ = SceneDocument();
    authoring_scene_document_loaded_ = false;
    play_scene_document_loaded_ = false;
    runtime_scene_dirty_ = false;
    play_mode_active_ = false;
    play_mode_paused_ = false;

    SceneDocument next_document;
    if (!next_document.LoadFromScenePath(scene_path)) {
        return false;
    }

    authoring_scene_document_ = std::move(next_document);
    authoring_scene_document_loaded_ = true;
    runtime_scene_dirty_ = true;
    SyncRuntimeMirrorIfDirty(engine);
    return true;
}

bool EditorSceneSession::HasSceneDocument() const {
    return play_mode_active_ ? play_scene_document_loaded_
                             : authoring_scene_document_loaded_;
}

SceneDocument &EditorSceneSession::GetSceneDocument() {
    return GetActiveSceneDocument();
}

const SceneDocument &EditorSceneSession::GetSceneDocument() const {
    return GetActiveSceneDocument();
}

SceneDocument &EditorSceneSession::GetAuthoringSceneDocument() {
    return authoring_scene_document_;
}

const SceneDocument &EditorSceneSession::GetAuthoringSceneDocument() const {
    return authoring_scene_document_;
}

bool EditorSceneSession::HasPlaySceneDocument() const {
    return play_scene_document_loaded_;
}


// EditorSceneSession public API: save / runtime mirroring
void EditorSceneSession::MarkRuntimeMirrorDirty() {
    if (!authoring_scene_document_loaded_) return;
    if (play_mode_active_) return;
    runtime_scene_dirty_ = true;
}

void EditorSceneSession::SaveDocumentIfDirty() {
    if (!authoring_scene_document_loaded_) return;
    if (!authoring_scene_document_.IsDirty()) return;
    authoring_scene_document_.Save();
}

/*
Runtime always consumes a copy of the editor cache. This keeps the
viewport live without making editor panels mutate runtime state directly.
*/
void EditorSceneSession::SyncRuntimeMirrorIfDirty(Engine &engine) {
    if (!authoring_scene_document_loaded_) return;
    if (play_mode_active_) return;
    if (!runtime_scene_dirty_) return;
    engine.LoadSceneAsset(authoring_scene_document_.GetSceneAsset());
    runtime_scene_dirty_ = false;
}

/*
Play mode snapshots the current document once, then runtime is free to
mutate its own world copy until Stop is pressed.
*/
bool EditorSceneSession::EnterPlayMode(Engine &engine) {
    if (!authoring_scene_document_loaded_) return false;
    if (play_mode_active_) return false;

    play_scene_document_ = authoring_scene_document_;
    play_scene_document_loaded_ = true;
    engine.LoadSceneAsset(play_scene_document_.GetSceneAsset());
    play_mode_active_ = true;
    play_mode_paused_ = false;
    runtime_scene_dirty_ = false;
    return true;
}

// Leaving play mode restores runtime from the authoritative editor cache.
bool EditorSceneSession::ExitPlayMode(Engine &engine) {
    if (!authoring_scene_document_loaded_) return false;
    if (!play_mode_active_) return false;

    play_mode_active_ = false;
    play_mode_paused_ = false;
    play_scene_document_ = SceneDocument();
    play_scene_document_loaded_ = false;
    runtime_scene_dirty_ = true;
    SyncRuntimeMirrorIfDirty(engine);
    return true;
}

bool EditorSceneSession::TogglePlayPause() {
    if (!authoring_scene_document_loaded_) return false;
    if (!play_mode_active_) return false;

    play_mode_paused_ = !play_mode_paused_;
    return true;
}

void EditorSceneSession::HandleSceneEditCommand( const SceneFormat::SceneEditCommand &command, Engine &engine) {
    if (command.empty()) return;

    /*
    Editor-mode mutations should patch the frozen runtime mirror in place.
    Rebuilding the whole scene on every property edit makes text input and
    transform tweaking feel broken because each keystroke tears runtime down and
    recreates it. Fall back to a full reload only if the incremental apply path
    rejects the command.
    */
    if (!engine.ApplySceneEditCommand(command)) {
        runtime_scene_dirty_ = true;
    }
}

void EditorSceneSession::HandleSceneEditCommands(
    const std::vector<SceneFormat::SceneEditCommand> &commands,
    Engine &engine) {
    for (const SceneFormat::SceneEditCommand &command : commands) {
        HandleSceneEditCommand(command, engine);
    }
}

/*
Called when the editor is closing: persist only editor document cache.
Runtime play-state mutations are discarded and never serialized.
*/
void EditorSceneSession::PersistDocumentOnEditorShutdown() {
    if (!authoring_scene_document_loaded_) return;
    play_mode_active_ = false;
    play_mode_paused_ = false;
    play_scene_document_ = SceneDocument();
    play_scene_document_loaded_ = false;
    runtime_scene_dirty_ = false;
    SaveDocumentIfDirty();
}

bool EditorSceneSession::IsPlayModeActive() const {
    return play_mode_active_;
}

bool EditorSceneSession::IsPlayModePaused() const {
    return play_mode_active_ && play_mode_paused_;
}

bool EditorSceneSession::IsSceneEditingEnabled() const {
    return HasSceneDocument();
}

bool EditorSceneSession::IsSceneSaveEnabled() const {
    return authoring_scene_document_loaded_ && !play_mode_active_;
}

SceneDocument &EditorSceneSession::GetActiveSceneDocument() {
    if (play_mode_active_ && play_scene_document_loaded_) {
        return play_scene_document_;
    }
    return authoring_scene_document_;
}

const SceneDocument &EditorSceneSession::GetActiveSceneDocument() const {
    if (play_mode_active_ && play_scene_document_loaded_) {
        return play_scene_document_;
    }
    return authoring_scene_document_;
}
