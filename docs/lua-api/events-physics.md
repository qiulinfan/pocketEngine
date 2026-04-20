# Events and Physics

## `Event`

| Function | Description |
| --- | --- |
| `Event.Publish(event_type, event_object)` | Publishes an event object |
| `Event.Subscribe(event_type, component_ref, function_ref)` | Subscribes a component callback |
| `Event.Unsubscribe(event_type, component_ref, function_ref)` | Removes a subscription |

The event bus is component-oriented: subscriptions are associated with the live
component reference that owns the callback.

## `Vector2`

`Vector2` is the Box2D-facing vector type exposed to Lua.

### Constructor

```lua
local v = Vector2(x, y)
```

### Properties

- `x`
- `y`

### Methods

| Method | Description |
| --- | --- |
| `v:Normalize()` | Normalizes the vector in place |
| `v:Length()` | Returns vector length |

### Static functions

| Function | Description |
| --- | --- |
| `Vector2.Distance(a, b)` | Distance between two vectors |
| `Vector2.Dot(a, b)` | Dot product |

### Operators

- `a + b`
- `a - b`
- `vector * scalar`
- `scalar * vector`

## `Collision`

Collision / trigger callbacks receive a `Collision` object with:

- `other`
- `point`
- `relative_velocity`
- `normal`

## `HitResult`

Physics raycasts return `HitResult` objects with:

- `actor`
- `point`
- `normal`
- `is_trigger`

## `Physics`

| Function | Description |
| --- | --- |
| `Physics.Raycast(position, direction, distance)` | Returns the first hit or `nil` |
| `Physics.RaycastAll(position, direction, distance)` | Returns all hits as a Lua array |
