# Arcbit — Phase 21: GUI / UI System

## Overview

The GUI system is a retained-mode, screen-space widget framework built on top of
the existing SDF text pipeline and sprite batcher. It handles all in-game UI:
HUD elements, menus, dialog boxes, inventory screens, and pause menus.

Guiding principles:
- **Screen-space only** — widgets render in pixel coordinates with no camera
  transform, matching the SDF pipeline convention.
- **No new GPU pipeline** — solid-color and textured quads reuse the existing UI
  sprite pipeline; a 1×1 white texture stands in for untextured quads.
- **Retained, not immediate** — widget trees are built once and mutated, not
  rebuilt every frame. Per-frame cost is proportional to what changed.
- **Data-driven layout** — widget trees are described in `.arcui` JSON files;
  behavior (callbacks, dynamic content) is wired in C++ after loading.

---

## Coordinate System

All UI positions and sizes are in **screen pixels**, origin at the **top-left**
corner of the window:

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
| `Size` | `Vec2` | Widget size in pixels. Can be overridden by `SizePercent`. |
| `SizePercent` | `Vec2` | If non-zero, overrides `Size` as a fraction of the parent's size (useful for bars, full-screen panels). |

---

## Widget Types

### `UIWidget` (base)

All widgets inherit from `UIWidget`. Core fields:

```cpp
std::string Name;           // optional; used by FindWidget<T>()
Vec2    Anchor      = {0, 0};
Vec2    Pivot       = {0, 0};
Vec2    Offset      = {0, 0};
Vec2    Size        = {100, 100};
Vec2    SizePercent = {0, 0};   // overrides Size per axis when > 0
f32     Opacity     = 1.0f;     // multiplied with children's opacity
bool    Visible     = true;
bool    Enabled     = true;     // disabled = not interactive, still drawn
i32     ZOrder      = 0;
bool    Focusable   = false;    // participates in keyboard/gamepad nav
```

### `Panel`

Solid-color background rect. `BackgroundColor` alpha=0 uses the skin default.

```cpp
bool  DrawBorder      = false;
Color BackgroundColor = {0, 0, 0, 0};
```

### `Overlay`

Full-screen `Panel` (pre-set to `SizePercent={1,1}`, `Anchor={0,0}`). Used as
a semi-transparent scrim behind modal screens.

### `NineSlice`

Scalable panel from a 9-patch texture. Source texture divided into 3×3 grid
by UV-space border fractions; corners are emitted at `Pixel*` sizes; center stretches.

```cpp
TextureHandle Texture; SamplerHandle Sampler;
Color Tint = {1,1,1,1};
f32 UVBorderLeft, UVBorderRight, UVBorderTop, UVBorderBottom; // 0–1 UV fractions
f32 PixelLeft, PixelRight, PixelTop, PixelBottom;             // screen pixels
```

### `Label`

Single- or multi-line SDF text.

```cpp
std::string Text;
TextAlign   Align      = TextAlign::Left;
bool        WordWrap   = false;
bool        AutoCenter = false;
Color       TextColor  = {0,0,0,0}; // {0,0,0,0} = use skin default
```

### `Button`

Clickable rect with label. Fires `OnClick` on mouse-up inside, or `OnActivate`
when confirmed via keyboard/gamepad.

```cpp
std::string            Text;
std::function<void()>  OnClick;
Color                  TextColor = {0,0,0,0};
```

States: Normal → Hovered → Pressed → Disabled. Colors resolved from `UISkin`.

### `Image`

Textured quad scaled to the widget rect.

```cpp
TextureHandle Texture; SamplerHandle Sampler;
Color  Tint = {1,1,1,1};
UVRect UV   = {0,0,1,1};
```

### `ProgressBar`

Two-layer horizontal fill bar.

```cpp
f32   Value     = 0.5f;
Color FillColor = {0,0,0,0}; // {0,0,0,0} = use skin default
```

### `ScrollPanel`

Clipped container with vertical scrolling and a drag scrollbar. Set
`ContentHeight` to the total logical height of all children.

```cpp
f32 ContentHeight  = 0.0f;
f32 ScrollOffset   = 0.0f;
f32 ScrollbarWidth = 6.0f;
```

Overrides `UpdateTree` and `CollectTree` to apply scissor clipping and scroll offset.

---

## .arcui File Format

`.arcui` files are JSON documents that describe a widget tree plus optional
metadata. They are loaded at runtime by `UILoader::Load`. The guiding principle
is **layout is data, behavior is code**: the file owns visual properties;
C++ wires `OnClick` and other callbacks after loading via `FindWidget<T>`.

### Top-level structure

```json
{
  "widgets": [ ... ],
  "meta":    { ... }
}
```

`widgets` is an array of root widget objects (equivalent to calling `Add<T>()` on
the screen). `meta` is a flat key-value object for style hints that C++ reads via
`GetMetaF32` / `GetMetaStr`.

### Widget object — base properties

Every widget object has a required `"type"` field and any subset of:

| Field          | Type         | Default      | Description                                                                                               |
|----------------|--------------|--------------|-----------------------------------------------------------------------------------------------------------|
| `type`         | string       | required     | `"Panel"`, `"Overlay"`, `"Label"`, `"Button"`, `"Image"`, `"NineSlice"`, `"ProgressBar"`, `"ScrollPanel"` |
| `name`         | string       | —            | Identifier for `FindWidget<T>(name)`                                                                      |
| `size`         | `[w, h]`     | `[100, 100]` | Pixel size                                                                                                |
| `size_percent` | `[x, y]`     | `[0, 0]`     | Overrides `size` as fraction of parent when > 0                                                           |
| `anchor`       | `[x, y]`     | `[0, 0]`     | Normalized point on parent rect                                                                           |
| `pivot`        | `[x, y]`     | `[0, 0]`     | Normalized point on this widget                                                                           |
| `offset`       | `[x, y]`     | `[0, 0]`     | Pixel offset after anchor/pivot alignment                                                                 |
| `zorder`       | int          | 0            | Sort key within screen layer                                                                              |
| `opacity`      | float        | 1.0          | Multiplied into children                                                                                  |
| `visible`      | bool         | true         |                                                                                                           |
| `enabled`      | bool         | true         |                                                                                                           |
| `focusable`    | bool         | false        | Keyboard/gamepad navigation                                                                               |
| `children`     | array        | —            | Nested widget objects                                                                                     |
| `tab_order`    | unsigned int | 0            | Sets focus keyboard/gamepad order                                                                         |

* Note: Defaults listed are the base defaults. Specific widgets may override them. I.e., default `focusable` for a dropdown is `true`
* All colors are `[r, g, b, a]` floats in 0–1 range.

### Per-type properties

**Panel / Overlay**

| Field | Type | Description |
|---|---|---|
| `background_color` | `[r,g,b,a]` | Fill color; alpha=0 uses skin default |
| `draw_border` | bool | Render skin border |

**Label**

| Field | Type | Description |
|---|---|---|
| `text` | string | Literal display text |
| `text_key` | string | Localization key — overrides `text` when present |
| `align` | `"left"` / `"center"` / `"right"` | Horizontal alignment |
| `text_color` | `[r,g,b,a]` | Alpha=0 uses skin default |
| `word_wrap` | bool | Wrap at widget width |
| `auto_center` | bool | Center text vertically in widget |

**Button**

| Field | Type | Description |
|---|---|---|
| `text` | string | Label text |
| `text_key` | string | Localization key |
| `text_color` | `[r,g,b,a]` | Alpha=0 uses skin default |

**ProgressBar**

| Field | Type | Description |
|---|---|---|
| `value` | float | Fill fraction 0–1 |
| `fill_color` | `[r,g,b,a]` | Alpha=0 uses skin default |

**ScrollPanel**

| Field | Type | Description |
|---|---|---|
| `content_height` | float | Total logical height of children |
| `scrollbar_width` | float | Scrollbar gutter width in pixels |

**NineSlice**

| Field | Type | Description |
|---|---|---|
| `tint` | `[r,g,b,a]` | Color tint |
| `uv_border_left/right/top/bottom` | float | UV-space border fractions (0–1) |
| `pixel_left/right/top/bottom` | float | Rendered border sizes in screen pixels |

**Image**

| Field | Type | Description |
|---|---|---|
| `tint` | `[r,g,b,a]` | Color tint |
| `uv` | `[u0,v0,u1,v1]` | UV rect |

### Meta section

The `meta` object holds arbitrary key-value pairs (`number` or `string`) that C++
reads via `GetMetaF32(key, default)` / `GetMetaStr(key, default)`. Use it to pass
style parameters to C++ that generates procedural content so the numbers live in
the file rather than in code:

```json
"meta": {
  "chip_h": 22,
  "chip_gap": 4,
  "key_col_x": 242,
  "ctrl_col_x": 454
}
```

### Localization

Labels and buttons support a `"text_key"` field. When present, `UILoader` passes
the key through `Loc::Get(key)` instead of using the literal `"text"` value.
This allows the same `.arcui` file to display in any language without modification.

```json
{ "type": "Button", "name": "resume-btn", "text_key": "ui.pause.resume" }
```

The fallback if a key has no translation is the key string itself, which makes
`.arcui` files readable as documentation even without a translation file loaded.

### Full example

```json
{
  "widgets": [
    { "type": "Overlay" },
    {
      "type": "Panel", "name": "bg",
      "size": [360, 280], "anchor": [0.5, 0.5], "pivot": [0.5, 0.5], "zorder": 1,
      "children": [
        {
          "type": "Label", "name": "title",
          "text_key": "ui.pause.title",
          "align": "center",
          "size": [360, 32], "anchor": [0.5, 0.0], "pivot": [0.5, 0.0],
          "offset": [0, 20], "zorder": 2
        },
        {
          "type": "Button", "name": "resume-btn",
          "text_key": "ui.pause.resume",
          "size": [240, 44], "anchor": [0.5, 0.0], "pivot": [0.5, 0.0],
          "offset": [0, 72], "focusable": true, "zorder": 2
        },
        {
          "type": "Button", "name": "quit-btn",
          "text_key": "ui.pause.quit",
          "text_color": [0.88, 0.30, 0.30, 1.0],
          "size": [240, 44], "anchor": [0.5, 0.0], "pivot": [0.5, 0.0],
          "offset": [0, 128], "focusable": true, "zorder": 2
        }
      ]
    }
  ],
  "meta": { "btn_gap": 56 }
}
```

### C++ integration pattern

```cpp
void PauseMenuScreen::OnEnter()
{
    // Load visual layout — clears existing roots and meta first
    if (!LoadLayout("assets/engine/ui/pause_menu.arcui"))
        BuildFallback(); // inline fallback if file absent

    // Wire behavior by name — the file owns layout, code owns logic
    if (auto* btn = FindWidget<Button>("resume-btn")) btn->OnClick = OnResume;
    if (auto* btn = FindWidget<Button>("quit-btn"))   btn->OnClick = OnQuit;

    // Read style hints for procedurally-generated content
    const f32 gap = GetMetaF32("btn_gap", 56.0f);
}
```

### Asset locations

Engine-provided `.arcui` files live in `engine/assets/ui/` and are copied to
`assets/engine/ui/` in the runtime output directory by the build system.

Game-specific layouts go in `game/assets/ui/`. To override an engine screen's
layout, set `LayoutPath` before pushing:

```cpp
pause->LayoutPath = "assets/ui/my_pause_menu.arcui";
GetUI().Push(std::move(pause));
```

### Custom widget types

Register game-side widget types before any `Load()` call that references them:

```cpp
UILoader::RegisterType("HealthOrb", [] { return std::make_unique<HealthOrb>(); });
```

Base properties (`size`, `anchor`, `name`, etc.) are applied automatically.
Type-specific properties must be handled by the widget's own deserialization
if needed (future: per-type `FromJson` hook).

---

### Custom UI with Lua scripting (future — Phase 28+)

The `.arcui` format is designed to eventually support Lua-driven behavior so
that modders and game authors can build complete screens — including callbacks —
without writing C++.

**Planned `.arcui` fields:**

```json
{
  "type": "Button", "name": "my-btn",
  "text_key": "ui.myscreen.action",
  "on_click": "MyScript.OnAction"
}
```

And at the screen level:

```json
{
  "widgets": [ ... ],
  "script": "scripts/ui/my_screen.lua"
}
```

**How it will work:**

1. `UILoader::Load` loads the `.lua` script specified in `"script"` and passes it
   to the Lua VM.
2. `"on_click": "Namespace.FunctionName"` wires a Lua function directly to
   `Button::OnClick`. The engine calls the Lua function when the button is clicked.
3. The Lua script can call engine-exposed APIs to push/pop screens, read game
   state, play sounds, etc.

**What stays in C++:**

- Engine-owned screens (`InputRebindScreen`, `AudioSettingsScreen`) keep their C++
  behavior wired after `LoadLayout`. They expose visual customization via `.arcui`
  (layout, colors, text) but their logic cannot be replaced with Lua.
- Performance-critical screens (e.g., HUD updated every tick) should remain C++.

**When to use each approach:**

| Scenario | Approach |
|---|---|
| Reskin or rearrange an engine screen | Edit its `.arcui` file |
| Add a new menu with simple button actions | `.arcui` + Lua script |
| Screens that read/write engine state heavily | C++ `UIScreen` subclass |
| Dynamic content (chip lists, scroll rows) | C++ `OnEnter` + `RebuildScroll` |

Until Lua scripting lands, game-specific screens are implemented as C++
`UIScreen` subclasses that call `LoadLayout` for visual layout and wire all
callbacks in `OnEnter`.

---

## Screen Management

`UIScreen` owns a widget tree and represents one logical UI layer.

Key methods:

```cpp
// Layout
bool      LoadLayout(std::string_view path);   // parse .arcui, populate tree
template<typename T>
T*        FindWidget(std::string_view name);   // find named widget, dynamic_cast
f32       GetMetaF32(std::string_view key, f32 def = 0.0f) const;
std::string GetMetaStr(std::string_view key, std::string_view def = "") const;

// Manual construction (when not using LoadLayout)
template<typename T> T* Add();
```

`UIManager` maintains a **stack** of active screens. The top-most screen receives
input; all visible screens are collected each frame. Screens with `BlocksInput=true`
prevent input from propagating to lower screens (used for pause menus, dialogs).

---

## Theming (UISkin)

`UISkin` is a plain data struct loaded from JSON (`LoadFromFile` / `SaveToFile`).
It defines defaults for every widget type; individual widgets override only what
they need.

Key fields:

```cpp
const FontAtlas* Font   = nullptr;
f32              FontScale = 1.0f;

Color PanelBg, PanelBorder;
Color ButtonNormal, ButtonHovered, ButtonPressed, ButtonDisabled;
Color TextNormal, TextDisabled, TextLabel;
Color ProgressBg, ProgressFill;
Color ScrollTrack, ScrollThumb, ScrollThumbHovered;
Color AccentColor;  // focused/listening states
Color OverlayColor; // Overlay widget default
i32   ScreenLayerBase; // set per-screen by UIManager; ensures stack order
```

---

## Rendering

`UIManager::CollectRenderData` walks the screen stack bottom-to-top and emits
quads via the UI sprite pipeline.

**Layer isolation:** Each screen on the stack gets `ScreenLayerBase = stackIndex * 1000`,
ensuring widgets from higher screens always sort above lower ones regardless of ZOrder.

**Opacity:** Parent opacity multiplies into children before emitting quads.

**Clipping:** `ScrollPanel` registers scissor rects and clips child quads to its bounds.

---

## File Structure

```
engine/
  include/arcbit/ui/
    UIManager.h
    UIScreen.h
    UIWidget.h
    UILoader.h              — .arcui parser, type registry
    Widgets.h               — Panel, Overlay, Label, Button, Image,
                              NineSlice, ProgressBar, ScrollPanel
    UISkin.h
    HudScreen.h
    PauseMenuScreen.h
    InputRebindScreen.h
    AudioSettingsScreen.h
    GraphicsSettingsScreen.h
  src/
    UIManager.cpp
    UIScreen.cpp
    UIWidget.cpp
    UILoader.cpp
    Widgets.cpp
    UISkin.cpp
    HudScreen.cpp
    PauseMenuScreen.cpp
    InputRebindScreen.cpp
    AudioSettingsScreen.cpp
    GraphicsSettingsScreen.cpp
  assets/ui/                — engine-provided .arcui defaults
    pause_menu.arcui
    audio_settings.arcui
    graphics_settings.arcui
    input_rebind.arcui
  assets/locale/            — engine locale strings
    en.json
```

Engine assets are copied to `assets/engine/ui/` in the runtime output directory.
Game assets go in `game/assets/ui/`.

---

## Implementation Phases

### Phase 21A — Foundation ✓
- [x] `UIWidget` base class: anchor/pivot/offset/size layout, opacity, visibility, Z-order, `Name`
- [x] `UIScreen`: widget tree, `Update`, `Collect`, `LoadLayout`, `FindWidget<T>`, meta accessors
- [x] `UIManager`: screen stack, `Push`/`Pop`, render collection, `HasBlockingScreen()`
- [x] `Panel`, `Overlay`, `Label`, `Button`, `Image`, `ProgressBar` widgets
- [x] Mouse input routing (hover + click)
- [x] Wire `UIManager` into `Application`
- [x] `UISkin` JSON loader

### Phase 21B — Polish ✓
- [x] `NineSlice` widget
- [x] Keyboard/gamepad focus navigation (tab order, confirm input)
- [x] Word wrap in `Label`, `AutoCenter`
- [x] Animated transitions: fade in/out per screen
- [x] `UIScreen::BlocksInput` + screen layer isolation (`ScreenLayerBase`)
- [x] `ScrollPanel` with scissor-rect clip + mouse wheel scroll
- [x] Engine-provided `HudScreen`, `PauseMenuScreen`, `InputRebindScreen`
- [x] Chip-based `InputRebindScreen`: multiple bindings per action, key/mouse/gamepad columns

### Phase 21C — Data-driven UI ✓ (in progress)
- [x] `.arcui` JSON format for widget tree serialization
- [x] `UILoader`: type registry, full property deserialization for all built-in types
- [x] `UIWidget::Name` + `FindDescendant` for post-load callback wiring
- [x] `UIScreen::LoadLayout`, `FindWidget<T>`, meta accessors
- [x] `PauseMenuScreen` migrated to `.arcui` with C++ fallback
- [x] `AudioSettingsScreen`, `GraphicsSettingsScreen` migrated to `.arcui` with `WireVolumeRow`/`FindWidget` callbacks
- [x] Engine asset deployment: `engine/assets/ui/` → `assets/engine/ui/` via CMake
- [x] Localization foundation: `Loc::Get(key)`, `text_key` field in UILoader, `en.json` engine locale
- [x] Migrate `AudioSettingsScreen`, `GraphicsSettingsScreen`, `InputRebindScreen` chrome to `.arcui`
- [ ] Editor integration: load/save `.arcui` from AvaloniaUI editor

### Phase 21D — Engine Screens (planned)
- [ ] `DialogScreen`: portrait + speaker name + scrolling text + choice buttons
- [ ] `SplashScreen`: ordered logo/image sequence before main menu
- [ ] `MainMenuScreen` stub: play, settings, quit
- [ ] `InventoryScreen` stub (populated by Phase 25 item system)
