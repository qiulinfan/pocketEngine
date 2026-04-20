# Rendering, Audio, and Input

## `Image`

### UI-space drawing

| Function | Description |
| --- | --- |
| `Image.DrawUI(image_name, x, y)` | Draws a UI image with default tint |
| `Image.DrawUIEx(image_name, x, y, r, g, b, a, sorting_order)` | Draws a UI image with tint and sorting order |

### World-space drawing

| Function | Description |
| --- | --- |
| `Image.Draw(image_name, x, y)` | Draws a world image with default pivot/tint |
| `Image.DrawEx(image_name, x, y, rotation_degrees, scale_x, scale_y, pivot_x, pivot_y, r, g, b, a, sorting_order)` | Full world-space image draw |
| `Image.DrawPixel(x, y, r, g, b, a)` | Queues a pixel draw |

## `Text`

### `Text.Draw(content, x, y, font_name, font_size, r, g, b, a)`

Draws screen-space text.

## `Audio`

| Function | Description |
| --- | --- |
| `Audio.Play(channel, clip_name, does_loop)` | Plays an audio clip on a channel |
| `Audio.Halt(channel)` | Stops playback on a channel |
| `Audio.SetVolume(channel, volume)` | Sets channel volume, clamped to `0..128` |

## `Input`

### Keyboard

| Function | Description |
| --- | --- |
| `Input.GetKey(keycode)` | Key held |
| `Input.GetKeyDown(keycode)` | Key pressed this frame |
| `Input.GetKeyUp(keycode)` | Key released this frame |

### Mouse

| Function | Description |
| --- | --- |
| `Input.GetMousePosition()` | Returns a `vec2` with `.x` and `.y` |
| `Input.GetMouseButton(button_num)` | Mouse button held |
| `Input.GetMouseButtonDown(button_num)` | Mouse button pressed this frame |
| `Input.GetMouseButtonUp(button_num)` | Mouse button released this frame |
| `Input.GetMouseScrollDelta()` | Scroll wheel delta for this frame |
| `Input.HideCursor()` | Hides the cursor |
| `Input.ShowCursor()` | Shows the cursor |

## `vec2`

`Input.GetMousePosition()` returns `vec2`, not `Vector2`.

It exposes:

- `.x`
- `.y`

Use `Vector2` for physics math and raycasts.
