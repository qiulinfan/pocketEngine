# <img src="docs/icon.png" alt="PocketEngine icon" width="10%"> PocketEngine 

PocketEngine is a cross-platform 2D runtime + editor game engine, written in C++17 on top of
SDL2, Lua, Box2D, Dear ImGui, and JSON scene assets. It hosts Lua for game logic scripting.
It is a variant of the [A2 Engine](https://a2engine.org/). For basic usage, check the documentation of A2 engine for the APIs.

PocketEngine Lua API documentation lives here:
- local source: [docs/lua-api/index.md](docs/lua-api/index.md)
- GitHub Pages: https://qiulinfan.github.io/pocketEngine/

> Below is a demo of the editor and runtime in action, running the example
> "ball game" included in `Projects/Default/`. This example game originates from the
> [A2 Engine](https://a2engine.org/) ecosystem.

![PocketEngine Demo](docs/show.gif)

The game has a Unity-like editor layout and runtime integration with:
- `SceneView` panel with actor-picking and drag&drop editing;
- embedded `Viewport` panel for pure runtime output;
- `Project` browser rooted at the currently opened project folder with scene opening and asset drag&drop support;
- `Hierarchy` tree with create, duplicate, delete, rename, actor reparenting;
- `Inspector` for component add/remove/rename and property editing
- actor parenting with local/world `Transform` and physics hierarchy inspection
- play-mode live editing and automatic scene-backed actor UID persistence
- `Sprite Editor` for creating and editing sprite assets.

## Build
### Linux Release
Install:
```bash
git clone https://github.com/qiulinfan/pocketEngine.git
```

Build:
```bash
cd pocketEngine
cmake --preset unix-makefiles-release
cmake --build --preset unix-makefiles-release -j4
```

Run the editor from the project root:
```bash
./pocket
```

Run the game executable:
```bash
./game
```

### Windows Release
Requires:
- Visual Studio 2022 with `Desktop development with C++`

Download the source from GitHub and extract it. Then open PowerShell in the extracted `pocketEngine` folder.

Build:

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs-release
```

Run the editor:

```powershell
.\pocket.exe
```

Run the game executable:

```powershell
.\game.exe
```

## Repository Layout and Architecture

- `src/app/runtime`, `src/app/editor`: executable hosts
- `src/engine/*`: runtime systems
- `src/editor/*`: editor shell, documents, panels
- `src/shared/*`: config, scene format, resource helpers
- `include/*`: public headers for the same layers
- `Projects/Default/*`: the default sample project
- `Projects/*`: local project slots; every project except `Default` is ignored by this engine repository
- `.engine/*`: editor-facing config, state, fonts, and icons
- `thirdparty/*`: dependencies

## Projects
PocketEngine suggests project management in the `Projects/` directory, butyou are also welcome to create projects anywhere and open them with the editor through `File -> Open Project...`.

There is a sample project(the ballgame) bundled with installation of the engine, located at `Projects/Default/`. The editor opens this project by default on startup, so it is recommended to keep it
 and the editor's default project root. 

`File -> New Project...` creates a complete minimal project. Linux users can type any local or absolute path, and Windows/macOS users can also use a system-native UI to choose the location.

## Architecture Illustration
A new project is initialized with `game.config`, `rendering.config`, `scenes/main.scene`, `actor_templates/empty.template`, and the standard project direcgtories: `audio/`, `component_types/`, `fonts/`, `images/`, `scenes/`, and `actor_templates/`.

All files under `docs/architecture/` are diagram-only for explaining the architecture.
- [Frame Pipeline](docs/architecture/frame-pipeline.md)
- [Asset Pipeline](docs/architecture/asset-pipeline.md)
- [Module Dependency](docs/architecture/module-dependency.md)
- [Editor Workflow](docs/architecture/editor-workflow.md)

AI-native editor development documents:
- [Implementation Spec](docs/development/ai-native-editor-spec.md)
- [Devlog](docs/development/ai-native-editor-devlog.md)

Pocket3D development documents:
- [Phase 1: Cross-platform OpenGL 3D Rendering](docs/development/pocket3d-phase-1-cross-platform-opengl.md)
- [Phase 1 Test Spec](docs/development/pocket3d-phase-1-test-spec.md)
- [Long-term Architecture Roadmap](docs/development/pocket3d-development-plan.md)

### Read-only AI Assistant (Phase 1)

Build the sidecar once with `npm --prefix tools/pocket-agent-host install`, then build and run the editor normally. The AI Assistant probes locally installed Codex and Claude Code CLIs and uses their existing login state.

Phase 1 binds a bearer-protected MCP server only on `127.0.0.1`. Agent sessions can inspect the current editor, scene, actors, component types, and project assets, but receive no PocketEngine write tools. The sidecar injects session-local MCP configuration and does not modify global Codex or Claude settings.

## Sample Project
`Projects/Default/` contains the default sample project.

## License

PocketEngine is released under the [MIT License](LICENSE).

Third-party libraries under `thirdparty/` keep their own upstream licenses.
