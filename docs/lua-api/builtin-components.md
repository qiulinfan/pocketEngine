# Built-in Components

Built-in components are first-class Lua classes and can also be added through
the editor or `actor:AddComponent(type_name)`.

## `Transform`

### Properties

- `actor`
- `key`
- `enabled`
- `x`
- `y`
- `rotation`

`Transform` stores local transform values. World transform is derived by the
engine and is not directly exposed as writable Lua properties.

## `SpriteRenderer`

### Properties

- `actor`
- `key`
- `enabled`
- `sprite`
- `r`
- `g`
- `b`
- `a`
- `pivot_x`
- `pivot_y`
- `scale_x`
- `scale_y`
- `sprite_row`
- `sprite_column`
- `sorting_order`
- `auto_sorting_order`

### Methods

| Method | Description |
| --- | --- |
| `SetSpriteCell(row, column)` | Convenience setter for `sprite_row` / `sprite_column` |

### Notes

- `sprite` is the image resource name, for example `images/Run.png`
- spritesheet slicing comes from project metadata in `.engine/project/spritesheets.json`
- custom gameplay components can store sub-sprite references as encoded strings,
  but `SpriteRenderer` itself still uses separate row / column fields

## `Rigidbody`

### Properties

- `actor`
- `key`
- `enabled`
- `x`
- `y`
- `body_type`
- `precise`
- `gravity_scale`
- `density`
- `angular_friction`
- `rotation`
- `has_collider`
- `has_trigger`
- `collider_type`
- `width`
- `height`
- `radius`
- `trigger_type`
- `trigger_width`
- `trigger_height`
- `trigger_radius`
- `friction`
- `bounciness`

### Methods

| Method | Description |
| --- | --- |
| `OnStart()` | Internal lifecycle hook |
| `OnDestroy()` | Internal lifecycle hook |
| `AddForce(vec)` | Adds force |
| `SetVelocity(vec)` | Sets linear velocity |
| `SetPosition(vec)` | Sets position |
| `SetRotation(degrees)` | Sets rotation |
| `SetAngularVelocity(value)` | Sets angular velocity |
| `SetGravityScale(value)` | Sets gravity scale |
| `SetUpDirection(vec)` | Sets orientation from up vector |
| `SetRightDirection(vec)` | Sets orientation from right vector |
| `GetPosition()` | Returns `Vector2` |
| `GetRotation()` | Returns rotation |
| `GetVelocity()` | Returns `Vector2` |
| `GetAngularVelocity()` | Returns angular velocity |
| `GetGravityScale()` | Returns gravity scale |
| `GetUpDirection()` | Returns `Vector2` |
| `GetRightDirection()` | Returns `Vector2` |

### Notes

PocketEngine derives an `effective_body_type` internally for dynamic actor
hierarchies. The authored Lua-visible `body_type` remains the requested value.

## `ParticleSystem`

### Properties

- `actor`
- `key`
- `enabled`
- `x`
- `y`
- `frames_between_bursts`
- `burst_quantity`
- `duration_frames`
- `start_scale_min`
- `start_scale_max`
- `start_speed_min`
- `start_speed_max`
- `rotation_min`
- `rotation_max`
- `rotation_speed_min`
- `rotation_speed_max`
- `start_color_r`
- `start_color_g`
- `start_color_b`
- `start_color_a`
- `end_color_r`
- `end_color_g`
- `end_color_b`
- `end_color_a`
- `emit_radius_min`
- `emit_radius_max`
- `emit_angle_min`
- `emit_angle_max`
- `gravity_scale_x`
- `gravity_scale_y`
- `drag_factor`
- `angular_drag_factor`
- `end_scale`
- `image`
- `sorting_order`

### Methods

| Method | Description |
| --- | --- |
| `OnStart()` | Internal lifecycle hook |
| `OnUpdate()` | Internal lifecycle hook |
| `OnDestroy()` | Internal lifecycle hook |
| `Stop()` | Stops emission |
| `Play()` | Starts emission |
| `Burst()` | Emits one burst immediately |
