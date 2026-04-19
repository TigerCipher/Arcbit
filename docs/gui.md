# Arcbit — Phase 21: GUI / UI System

## Overview

The GUI system is a retained-mode, screen-space widget framework built on top of
the existing SDF text pipeline and sprite batcher. It handles all in-game UI:
HUD elements, menus, dialog boxes, inventory screens, and pause menus.

Guiding principles:
- **Screen-space only** — widgets render in pixel coordinates with no camera
  transform, matching the SDF pipeline convention.
- **No new GPU pipeline** — solid-color and textured quads reuse the existing SDF
  sprite pipeline; a 1×1 white texture stands in for untextured quads.
- **Retained, not immediate** — widget trees are built once and mutated, not
  rebuilt every frame. This keeps per-frame cost proportional to what changed,
  not to the total widget count.
- **Thin render layer** — the UI system outputs `SDFSprites` (and eventually
  regular `Sprites` for world-space widgets). No new FramePacket lists needed.

---

## Coordinate System

All UI positions and sizes are in **screen pixels**, origin at the **top-left**
corner of the window (matching the SDF text pipeline):

```
(0,0) ──────────────── (W, 0)
  │                       │
  │       screen          │
  │                       │
(0,H) ──────────────── (W, H)
```

Widget positions are expressed as an **anchor** on the parent (or screen) plus
a **pixel offset**. This lets the same layout adapt to different window sizes
without hardcoding pixel positions.

---

## Anchoring & Layout

Each widget has:

| Field | Type | Description |
|---|---|---|
| `Anchor` | `Vec2` | Normalized (0–1) point on the **parent** rect. `{0,0}` = top-left, `{0.5,0.5}` = center, `{1,1}` = bottom-right. |
| `Pivot` | `Vec2` | Normalized point on the **widget itself** that aligns to the anchor. Defaults to `{0,0}` (top-left). |
| `Offset` | `Vec2` | Pixel displacement added after anchor/pivot alignment. |
| `Size` | `Vec2` | Widget size in pixels. Can be overridden by stretch mode. |
| `SizePercent` | `Vec2` | If non-zero, overrides `Size` as a fraction of the parent's size (useful for bars, full-screen panels). |

**Examples:**

```
// Centered on screen
anchor = {0.5, 0.5}, pivot = {0.5, 0.5}, offset = {0, 0}

// Bottom-right corner, 8px margin
anchor = {1, 1}, pivot = {1, 1}, offset = {-8, -8}

// Top bar spanning full width, 40px tall
anchor = {0, 0}, pivot = {0, 0}, size = {0, 40}, sizePercent = {1, 0}
```

Layout is computed top-down: a parent resolves its own rect first, then children
resolve relative to it. Computed rects are cached and invalidated when any
ancestor's position, size, or visibility changes.

---

## Widget Types

### `UIWidget` (base)

All widgets inherit from `UIWidget`. Core fields:

```cpp
Vec2    Anchor      = {0.0f, 0.0f};
Vec2    Pivot       = {0.0f, 0.0f};
Vec2    Offset      = {0.0f, 0.0f};
Vec2    Size        = {0.0f, 0.0f};
Vec2    SizePercent = {0.0f, 0.0f}; // overrides Size per axis when > 0
f32     Opacity     = 1.0f;         // multiplied with children's opacity
bool    Visible     = true;
bool    Enabled     = true;         // disabled = not interactive, still drawn
i32     ZOrder      = 0;            // sort within the same screen layer
```

Children are stored in a `std::vector<std::unique_ptr<UIWidget>>`. The tree is
owned by a `UIScreen`.

### `Panel`

A solid-color or textured rectangular background. The building block for all
container widgets.

```cpp
Color         BackgroundColor = Color::Transparent();
TextureHandle Texture;          // leave invalid for solid color
SamplerHandle Sampler;
```

### `NineSlice`

A scalable panel that preserves corner pixel art regardless of widget size.
The source texture is divided into a 3×3 grid by four border measurements.

```cpp
TextureHandle Texture;
SamplerHandle Sampler;
f32           BorderLeft, BorderRight, BorderTop, BorderBottom; // pixels in source texture
```

Nine-slice is rendered as 9 individual quads. Corners are fixed size; edges
scale in one axis; center scales in both.

### `Label`

Renders a text string using a `FontAtlas`.

```cpp
std::string  Text;
FontAtlas*   Font       = nullptr; // defaults to engine debug font if null
f32          FontScale  = 1.0f;
Color        TextColor  = Color::White();
TextAlign    Align      = TextAlign::Left;
bool         WordWrap   = false;   // Phase 21B — wrap at widget width
```

### `Image`

A textured quad. Unlike `Panel`, `Image` defaults to filling its size exactly
and has no background color — it is purely a sprite.

```cpp
TextureHandle Texture;
SamplerHandle Sampler;
UVRect        UV    = {0, 0, 1, 1};
Color         Tint  = Color::White();
```

### `Button`

A `Panel` + `Label` composite with hover / pressed / disabled state. Fires a
callback on click (or confirm input when focused via keyboard/gamepad).

```cpp
std::string          Text;
std::function<void()> OnClick;

// Per-state overrides (resolved from UISkin if not set explicitly)
Color NormalColor;
Color HoverColor;
Color PressedColor;
Color DisabledColor;
```

Interaction states:
- **Normal** — default
- **Hover** — cursor over widget (mouse) or focus ring (keyboard/gamepad)
- **Pressed** — mouse button held or confirm input held
- **Disabled** — `Enabled = false`

### `ProgressBar`

A two-layer bar: background rect + filled foreground rect.

```cpp
f32   Value       = 1.0f;   // 0–1 fill fraction
Color FillColor   = {0.2f, 0.8f, 0.2f, 1.0f};
Color BackColor   = {0.1f, 0.1f, 0.1f, 0.8f};
bool  Horizontal  = true;   // false = vertical fill (bottom-up)
```

### `ScrollPanel` *(Phase 21B)*

A clipped container with optional scrollbar for lists longer than the panel height.

---

## Screen Management

A `UIScreen` owns a widget tree and represents one logical UI layer (HUD, pause
menu, dialog, inventory, etc.).

```cpp
class UIScreen {
public:
    void Update(f32 dt);
    void CollectRenderData(FramePacket& packet);
    void OnInputEvent(const InputEvent& e, bool& consumed);

    template<typename T, typename... Args>
    T* AddWidget(Args&&... args);   // adds to the root

    void Show();
    void Hide();
    bool IsVisible() const;
};
```

The `UIManager` maintains a **stack** of active screens. Screens deeper in the
stack are still rendered (so the HUD stays visible behind a pause menu) but may
optionally block input from propagating further down.

```cpp
class UIManager {
public:
    void PushScreen(std::string_view name);
    void PopScreen();
    void PopTo(std::string_view name);  // pop until named screen is on top
    UIScreen* GetScreen(std::string_view name);

    void Update(f32 dt);
    void CollectRenderData(FramePacket& packet);

    // Called before game input — returns true if the event was consumed.
    bool RouteInput(const InputEvent& e);
};
```

All named screens are registered at startup (in `OnStart`). Pushing a screen
that is already in the stack brings it to the top without duplication.

**Example screen stack during gameplay:**

```
[top]  PauseMenu  — blocks input to layers below
       HUD        — renders health bar, minimap, hotbar
[bot]  (world)    — game input reaches here only if nothing above consumed it
```

---

## Input Routing

The `UIManager::RouteInput` is called **before** `InputManager::ProcessEdges` in
the game loop (inside `Application::Run`). If a widget consumes the event, the
game never sees it.

**Mouse input:**
- Each frame, compute which widget (if any) the cursor is over (top-most,
  highest Z-order, in the top-most visible screen).
- Hover state is updated continuously.
- Click fires `Button::OnClick` on mouse-up when the cursor is still within the
  widget.

**Keyboard / gamepad navigation:**
- A **focus ring** tracks the currently focused widget (navigated with
  directional input — D-pad or arrow keys).
- `Tab` / `Shift+Tab` move focus forward/backward through the focusable widget
  list in the active screen.
- Confirm input (Enter / gamepad A) activates the focused widget.
- The focused screen layer is always the top of the stack.

---

## Rendering

`UIManager::CollectRenderData` walks the screen stack bottom-to-top (so top
screens draw over lower ones) and emits quads into `packet.SDFSprites`.

Each widget type maps to primitives:

| Widget | Primitives emitted |
|---|---|
| `Panel` | 1 quad (white texture + tint, or actual texture) |
| `NineSlice` | 9 quads |
| `Label` | N quads via `DrawText` (SDF mode) |
| `Image` | 1 quad |
| `Button` | 1 quad (background) + N quads (label) |
| `ProgressBar` | 2 quads |

**Z ordering:** Each widget's `ZOrder` is added to a per-screen base layer
offset (e.g. HUD = layer 10000, PauseMenu = layer 20000). The existing sprite
layer sort handles correct draw order without a separate pass.

**Opacity:** Parent opacity multiplies into children before emitting quads. A
panel at 50% opacity with a label child makes the label 50% opaque too.

**Clipping:** Phase 21A uses no per-widget clip rects (quads simply overdraw).
Phase 21B adds scissor rect clipping for `ScrollPanel` and modal dialog masks.

---

## Theming (UISkin)

A `UISkin` is a plain data struct loaded from JSON. It defines defaults for
every widget type so individual widgets don't need per-field color assignments.

```cpp
struct UISkin {
    FontAtlas* DefaultFont  = nullptr;
    f32        DefaultScale = 1.0f;

    struct PanelSkin   { Color Background; Color Border; f32 BorderWidth; };
    struct ButtonSkin  { Color Normal, Hover, Pressed, Disabled; Color TextColor; };
    struct LabelSkin   { Color TextColor; };
    struct BarSkin     { Color Fill, Background; };

    PanelSkin  Panel;
    ButtonSkin Button;
    LabelSkin  Label;
    BarSkin    ProgressBar;

    // Nine-slice source texture for panel/button backgrounds (optional).
    TextureHandle PanelTexture;
    SamplerHandle PanelSampler;
    f32 BorderLeft, BorderRight, BorderTop, BorderBottom;
};
```

`UIManager` holds the active skin. Widgets fall back to skin defaults for any
field they don't override explicitly.

---

## Integration Points

### Application

`UIManager` is owned by `Application` alongside `InputManager` and `TextureManager`.
The game loop wires it in three places:

```cpp
// 1. Before input processing — UI consumes first
if (!_ui.RouteInput(e)) { /* game input */ }

// 2. Fixed-timestep tick
_ui.Update(_fixedTimestep);

// 3. Render — after OnRender and CollectRenderData
_ui.CollectRenderData(packet);
```

`GetUI()` accessor on `Application` lets game code reach `UIManager` from
`OnStart` to register screens and wire button callbacks.

### Font

Labels default to the engine's `_debugFont` (Roboto SDF) if no font is set.
Games load their own `FontAtlas` in `OnStart` and assign it to `UIManager`'s
skin or individual `Label` widgets.

### TextureManager

Nine-slice and Image textures are loaded via `GetTextures()` the same way as
sprite textures.

---

## File Structure

```
engine/
  include/arcbit/ui/
    UIManager.h
    UIScreen.h
    UIWidget.h
    Widgets.h        — Panel, Label, Button, Image, ProgressBar, NineSlice
    UISkin.h
  src/
    UIManager.cpp
    UIScreen.cpp
    UIWidget.cpp
    Widgets.cpp
```

`arcbit-engine` gains these sources; no new CMake targets needed.

---

## Implementation Phases

### Phase 21A — Foundation
- [ ] `UIWidget` base class: anchor/pivot/offset/size layout, opacity, visibility, Z-order
- [ ] `UIScreen`: widget tree, `CollectRenderData`, `Update`
- [ ] `UIManager`: screen stack, `PushScreen`/`PopScreen`, render collection
- [ ] `Panel` widget (solid color + optional texture)
- [ ] `Label` widget (SDF text via existing `DrawText`)
- [ ] `Button` widget (hover/pressed states, `OnClick` callback)
- [ ] `Image` widget
- [ ] `ProgressBar` widget
- [ ] Mouse input routing (hover + click)
- [ ] `UISkin` with JSON loader
- [ ] Wire `UIManager` into `Application`
- [ ] Simple HUD screen: health bar + FPS label

### Phase 21B — Polish
- [ ] `NineSlice` widget
- [ ] Keyboard/gamepad focus navigation (tab order, confirm input)
- [ ] `ScrollPanel` with clip rect
- [ ] Word wrap in `Label`
- [ ] Animated transitions: fade in/out per screen (lerp opacity on push/pop)
- [ ] Input rebinding screen (reuses Phase 9 runtime rebind API)
- [ ] Pause menu screen

### Phase 21C — HUD & Menus
- [ ] HUD screen: health bar, mana bar, hotbar slots, minimap placeholder
- [ ] Dialog box screen (used by Phase 27 dialog system)
- [ ] Splash screen system: ordered logo sequence before main menu
- [ ] Main menu screen stub
