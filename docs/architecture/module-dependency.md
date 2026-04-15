# Module Dependency

This is a high-level architecture graph, not a full include graph.

## Current Layered Graph

```mermaid
graph TD
    subgraph App["App Layer (src/app)"]
        RuntimeApp[runtime app]
        EditorApp[editor app]
    end

    subgraph Editor["Editor Layer (src/editor)"]
        EditorCore[editor/core]
        EditorDoc[editor/documents]
        EditorPanels[editor/panels]
    end

    subgraph Engine["Engine Layer (src/engine)"]
        EngineCore[engine/core]
        EngineScene[engine/scene]
        EngineScript[engine/scripting]
        EngineRender[engine/rendering]
        EnginePhysics[engine/physics]
        EngineParticles[engine/particles]
        EngineAudio[engine/audio]
        EngineInput[engine/input]
    end

    subgraph Shared["Shared Layer (src/shared + include/shared)"]
        SharedConfig[shared/config]
        SharedSceneFormat[shared/scene_format]
        SharedResource[shared/resources]
    end

    subgraph Assets["Resources"]
        Resources[resources/*]
    end

    RuntimeApp --> EngineCore
    EditorApp --> EditorCore
    EditorCore --> EditorDoc
    EditorCore --> EditorPanels
    EditorCore --> EngineCore

    EditorDoc --> SharedSceneFormat
    EditorPanels --> EditorDoc
    EditorPanels --> SharedSceneFormat

    EngineCore --> EngineScene
    EngineCore --> EngineScript
    EngineCore --> EngineRender
    EngineCore --> EngineParticles
    EngineCore --> EngineInput
    EngineCore --> SharedConfig
    EngineCore --> SharedSceneFormat

    EngineScene --> EngineScript
    EngineScene --> SharedSceneFormat
    EngineScript --> EnginePhysics
    EngineScript --> EngineParticles
    EngineScript --> SharedResource
    EngineRender --> EngineParticles
    EngineRender --> SharedResource

    SharedSceneFormat --> SharedResource

    Resources --> SharedConfig
    Resources --> SharedSceneFormat
    Resources --> EngineScript
    Resources --> EngineRender
    Resources --> EngineAudio
```

## Practical Notes

- The runtime host and editor host are separate executables.
- `shared/scene_format` is the bridge shared by runtime scene loading, editor
  documents, and mutation types.
- `SceneMutation` / `SceneEditCommand` form the shared contract between editor
  UI, document updates, and play-mode runtime sync.
- `engine/scripting` remains the largest integration hub because it owns Lua
  component creation, lifecycle ordering, component queries, and much of the
  runtime mutation reconciliation.
- `shared/resources` is still a light helper layer rather than a full asset
  database.
