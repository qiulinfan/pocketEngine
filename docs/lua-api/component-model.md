# Component Model

PocketEngine gameplay code is built around Lua component tables.

## Declaring a component type

Each component type lives in `resources/component_types/*.lua` and is declared
as a global table whose name matches the file name.

```lua
Mover = {
    speed = 4.0,
    target_names = {},
}
```

## Instance fields automatically provided by the engine

Every live component instance receives:

- `self.actor`
- `self.key`
- `self.enabled`

The instance keeps its own state, while functions and default values are looked
up through the component type table.

## Lifecycle callbacks

The runtime recognizes these callback names when they exist on a component:

- `OnStart()`
- `OnUpdate()`
- `OnLateUpdate()`
- `OnDestroy()`
- `OnCollisionEnter(collision)`
- `OnCollisionExit(collision)`
- `OnTriggerEnter(collision)`
- `OnTriggerExit(collision)`

## Authoring defaults vs runtime overrides

A component property can come from three places:

1. the Lua component type default
2. scene/template overrides stored in JSON
3. runtime edits applied to the live instance

PocketEngine now uses the same array-aware property writer for both:

- scene/template override application
- live runtime property editing

so arrays and scalars stay consistent across editor and runtime.

## Arrays

Arrays must stay homogeneous:

- `bool[]`
- `int[]`
- `double[]`
- `string[]`

Good:

```lua
frames = {"sprite://images/Run.png?row=1&column=1",
          "sprite://images/Run.png?row=1&column=2"}
```

Not supported:

- mixed-type tables
- nested tables as property values
- map-like tables as serialized component properties

## Built-in component names

These component types are built into the engine and can be added from Lua or
the editor:

- `Transform`
- `SpriteRenderer`
- `Rigidbody`
- `ParticleSystem`
