# <img src="docs/icon.png" alt="PocketEngine icon" width="10%"> PocketEngine 

PocketEngine is a cross-platform 2D runtime + editor game engine, written in C++17 on top of
SDL2, Lua, Box2D, Dear ImGui, and JSON scene assets. It hosts Lua for game logic scripting.
It is a variant of the [A2 Engine](https://a2engine.org/). For basic usage, check the documentation of A2 engine for the APIs.

PocketEngine Lua API documentation lives here:
- local source: [docs/lua-api/index.md](docs/lua-api/index.md)
- GitHub Pages: https://qiulinfan.github.io/pocketEngine/

> Below is a demo of the editor and runtime in action, running the example
> "ball game" included in `resources/`. This example game originates from the
> [A2 Engine](https://a2engine.org/) ecosystem.

![PocketEngine Demo](docs/show.gif)

The game has a Unity-like editor layout and runtime integration with:
- `SceneView` panel with actor-picking and drag&drop editing;
- embedded `Viewport` panel for pure runtime output;
- `Project` browser rooted at `resources/` with scene opening and asset drag&drop support;
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
cd pocketEngine
cmake --preset unix-makefiles-release
cmake --build --preset unix-makefiles-release -j4
```
Run from the project root:
```bash
./pocket
./game
```

### Windows Release
Requires:
- Visual Studio 2022 with `Desktop development with C++`
- Git in `PATH`

#### Method 1: Manual Clone, Build, Run

Clone this repository and enter the repo in PowerShell:

```powershell
git clone https://github.com/qiulinfan/pocketEngine.git
cd .\pocketEngine
```

Build:

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs-release
```

Run:

```powershell
.\pocket.exe
.\game.exe
```

#### Method 2: Windows Quick Install

If you want a one-shot Windows install, the PowerShell helper below:
- clones or updates PocketEngine into `C:\ProgramData\PocketEngine`
- configures and builds the `Release` preset
- creates `PocketEngine Editor` and `PocketEngine Runtime` shortcuts on the current user's desktop

Run PowerShell as Administrator if you want to keep the default `C:\ProgramData\PocketEngine` install path and have the shortcuts created on Desktop:

```powershell
$script = Join-Path $env:TEMP "win_install.ps1"
Invoke-WebRequest "https://raw.githubusercontent.com/qiulinfan/pocketEngine/main/scripts/win_install.ps1" -OutFile $script
powershell -ExecutionPolicy Bypass -File $script
```

## Repository Layout and Architecture

- `src/app/runtime`, `src/app/editor`: executable hosts
- `src/engine/*`: runtime systems
- `src/editor/*`: editor shell, documents, panels
- `src/shared/*`: config, scene format, resource helpers
- `include/*`: public headers for the same layers
- `resources/*`: the project space and you design it!
- `.engine/*`: editor-facing config, state, fonts, and icons
- `thirdparty/*`: dependencies

All files under `docs/architecture/` are diagram-only for explaining the architecture.
- [Frame Pipeline](docs/architecture/frame-pipeline.md)
- [Asset Pipeline](docs/architecture/asset-pipeline.md)
- [Module Dependency](docs/architecture/module-dependency.md)
- [Editor Workflow](docs/architecture/editor-workflow.md)
## Sample Project
`resources_ballgame_example/` contains a sample project with a simple ball game and some sprites. You can open the scene `ballgame.scene` in the editor and run it to see how it works. The Lua script for the ball game is located at `scripting` subdirectory of the same folder.

## License

PocketEngine is released under the [MIT License](LICENSE).

Third-party libraries under `thirdparty/` keep their own upstream licenses.
