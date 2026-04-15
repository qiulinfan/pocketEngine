# PocketEngine

PocketEngine is a 2D game engine and editor built in C++17 on SDL2, Lua,
Box2D, Dear ImGui, and JSON assets loaded directly from `resources/`.

[Chinese / 中文版](README.zh-CN.md)

## Overview

The repository ships two hosts:

- `game_engine`: standalone runtime player
- `game_editor`: docked GUI editor with an embedded runtime viewport

The current editor workflow is document-first:

- GUI edits mutate a `SceneDocument` owned by `EditorSceneSession`
- In edit mode, the runtime preview is refreshed from the document at frame
  boundaries
- In play mode, the session snapshots the authoring document into a temporary
  play sandbox document
- Play-mode GUI edits mutate the sandbox document and are also applied to the
  live runtime through `SceneEditCommand` / `SceneMutation`
- Stopping play discards the sandbox and restores the authoring preview

## Editor Highlights

- Docked Dear ImGui shell with menu bar, status panels, and embedded viewport
- `Project` panel for browsing `resources/`, opening `.scene` files, and
  launching configured external editors
- `Hierarchy` panel for actor selection, creation, duplication, deletion, and
  rename
- `Inspector` panel for component add/remove/rename, type changes, and scalar
  property editing
- `Viewport` panel with floating play / pause / stop controls and runtime input
  routing
- Live runtime scene editing in play mode through scene mutations

## Repository Layout

- `src/app/runtime` and `src/app/editor`: executable hosts
- `src/engine/*`: runtime systems
- `src/editor/*`: editor shell, scene session, documents, and panels
- `src/shared/*`: config, resource helpers, scene-format, and mutation types
- `resources/*`: scenes, templates, Lua component types, images, fonts, audio,
  and configs
- `docs/*`: architecture and editor workflow notes

## Build

CMake presets are the primary build entry point.

### Linux

```bash
cmake --preset unix-makefiles-debug
cmake --build --preset unix-makefiles-debug -j4
./build/unix-makefiles-debug/game_engine_linux
./build/unix-makefiles-debug/game_editor_linux
```

### Windows

```bash
cmake --preset vs2022-x64
cmake --build --preset vs2022-debug
cmake --build --preset vs2022-release
```

### macOS

```bash
cmake --preset xcode
cmake --build --preset xcode-debug
cmake --build --preset xcode-release
```

`build_commands.txt` contains a short copy/paste list for common local flows.

## Scene + Runtime Model

- `.scene` files are parsed by `shared/scene_format`
- `SceneDocument` is the editor-side mutable cache for one scene
- `EditorSceneSession` owns the authoritative authoring document and, when play
  mode is active, a temporary play document
- `SceneMutation` and `SceneEditCommand` are the shared edit contract between
  editor panels, the document layer, and play-mode runtime sync
- Runtime actors carry editor-side stable actor UIDs so play-mode mutations can
  target the correct live actor

## Save Behavior

- Scene edits stay in memory inside the authoring document until save
- The scene file is written on explicit save, scene switch, or editor shutdown
- Play-mode sandbox edits are never written back to the authoring document or
  `.scene` file

## Documentation

- [Editor Overview](docs/editor-overview.md)
- [Editor Overview (Chinese)](docs/editor-overview.zh-CN.md)
- [Frame Pipeline](docs/architecture/frame-pipeline.md)
- [Asset Pipeline](docs/architecture/asset-pipeline.md)
- [Module Dependency](docs/architecture/module-dependency.md)
