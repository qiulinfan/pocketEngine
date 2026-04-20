# Lua API Overview

PocketEngine exposes its Lua APIs from the files under
`src/engine/scripting/luaapi/`. The runtime loads those registrations once, and
every component or gameplay script sees the same global namespaces and built-in
classes.

## Main namespaces

| Area | Entry points |
| --- | --- |
| Application and logging | `Application.*`, `Debug.*` |
| Scene management | `Scene.*`, `Camera.*` |
| Actors and components | `Actor`, `Actor.*` |
| Rendering | `Image.*`, `Text.*` |
| Audio | `Audio.*` |
| Input | `Input.*`, `vec2` |
| Events | `Event.*` |
| Physics | `Physics.*`, `Vector2`, `Collision`, `HitResult` |
| Built-in components | `Transform`, `SpriteRenderer`, `Rigidbody`, `ParticleSystem` |

## Important conventions

### 1. Component scripts are Lua tables

A component type is a table loaded from `resources/component_types/*.lua`. Its
fields become:

- default property values
- lifecycle callbacks
- helper functions

Each live component instance inherits from its type table through Lua
metatables.

### 2. Supported property types

PocketEngine currently supports these property shapes:

- `bool`
- `int`
- `double`
- `string`
- homogeneous arrays of those scalar types

Examples:

```lua
MyComponent = {
    enabled = true,
    speed = 4.0,
    tags = {"enemy", "flying"},
    frame_times = {0.1, 0.08, 0.08, 0.1},
}
```

### 3. Arrays use normal Lua tables

On the Lua side, arrays are ordinary 1-based tables. You can iterate them with
`ipairs()`. The engine bridges them back into scene JSON and editor properties
when they stay homogeneous.

### 4. Sprite references in custom string properties

Dragging a sub-sprite from the Project panel into a custom `string` or
`string[]` property stores a sprite reference string like this:

```txt
sprite://images/Run.png?row=1&column=3
```

This is mainly for custom gameplay components. The built-in `SpriteRenderer`
still uses:

- `sprite`
- `sprite_row`
- `sprite_column`

as separate properties.
