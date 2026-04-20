# Application, Scene, and Camera

## `Debug`

### `Debug.Log(message)`

Logs a string to the runtime console/stdout.

## `Application`

### `Application.Quit()`

Immediately exits the process.

### `Application.Sleep(milliseconds)`

Sleeps the current thread. Negative values are clamped to `0`.

### `Application.GetFrame() -> int`

Returns the current frame number from the engine frame clock.

### `Application.OpenURL(url)`

Asks the host OS to open a URL with the default browser/app.

## `Scene`

### `Scene.Load(scene_name)`

Requests loading a scene by name.

### `Scene.GetCurrent() -> string`

Returns the current scene name.

### `Scene.DontDestroy(actor)`

Marks an actor so it survives scene transitions.

## `Camera`

### `Camera.SetPosition(x, y)`

Sets the runtime camera world position.

### `Camera.GetPositionX() -> float`

Returns the runtime camera X position.

### `Camera.GetPositionY() -> float`

Returns the runtime camera Y position.

### `Camera.SetZoom(zoom_factor)`

Sets the runtime camera zoom.

### `Camera.GetZoom() -> float`

Returns the runtime camera zoom.
