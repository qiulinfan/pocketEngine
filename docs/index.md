# PocketEngine Docs

![PocketEngine Demo](show.gif)

PocketEngine is a cross-platform 2D runtime + editor game engine written in
C++17 on top of SDL2, Lua, Box2D, Dear ImGui, and JSON scene assets. It is a
variant of the [A2 Engine](https://a2engine.org/), but the runtime/editor
integration and several Lua-facing APIs have evolved in PocketEngine-specific
ways.

## What is documented here

- The current **Lua API surface** exposed by `src/engine/scripting/luaapi/*`
- The **component model** used by Lua component types
- The **built-in components** available to both runtime and editor
- Architecture diagrams for the runtime/editor pipeline

## Quick links

- [Lua API Overview](lua-api/index.md)
- [Actor API](lua-api/actor.md)
- [Built-in Components](lua-api/builtin-components.md)
- [Editor Workflow](architecture/editor-workflow.md)

## Local workflow

To preview the documentation locally:

```bash
mkdocs serve
```

To build the static site:

```bash
mkdocs build --clean
```

To publish the documentation to `gh-pages`:

```bash
make docs
```
