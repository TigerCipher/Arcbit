# Arcbit — Phase 40: AvaloniaUI Editor

## Overview

The Arcbit editor is a standalone C# AvaloniaUI application. It is a **remote
control** for the engine, not a reimplementation of it. All rendering — the
scene canvas, UI preview, particle preview, etc. — is done by the real engine.
The editor owns property panels, asset browsers, node canvases, and tool state;
the engine owns pixels.

---

## Architecture — `arcbit-editor-host`

### The problem with DLLs-per-editor-type

A separate unmanaged DLL for each editor panel (UI previewer, scene canvas,
etc.) would give each panel its own Vulkan device, can't share GPU resources,
and multiplies managed-to-native interop boundaries with no benefit.

### The solution — one host process, one GPU device

The editor spawns a single native child process: **`arcbit-editor-host`**.
This is the Phase 38 generic runtime (no game-specific code) configured to
accept editor commands instead of loading a `project.arcbit` directly.

```
┌─────────────────────────────────────────────────────┐
│  C# AvaloniaUI Editor                               │
│                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────┐  │
│  │ Property     │  │ Asset        │  │ Log /    │  │
│  │ panels       │  │ Browser      │  │ Events   │  │
│  │ (pure C#)    │  │ (pure C#)    │  │ (pure C#)│  │
│  └──────────────┘  └──────────────┘  └──────────┘  │
│                                                     │
│  ┌──────────────────────────────────────────────┐   │
│  │  NativeControlHost (embedded engine window)  │   │
│  └──────────────────────────────────────────────┘   │
│           ▲ SetParent / resize events               │
└───────────┼─────────────────────────────────────────┘
            │  Named pipe (JSON messages, bidirectional)
┌───────────▼─────────────────────────────────────────┐
│  arcbit-editor-host  (C++ engine runtime)           │
│                                                     │
│  SDL window ──► Vulkan swapchain                    │
│  One GPU device, one render thread                  │
│  Modes: scene | ui_preview | particle | etc.        │
└─────────────────────────────────────────────────────┘
```

### Window embedding

On Windows the host's SDL window is embedded into the editor via Win32
`SetParent()`. Avalonia's `NativeControlHost` provides the host HWND to parent
into. The editor sends resize commands over the pipe when its panel resizes; the
host forwards those to its SDL window.

Fallback (cross-platform): the host renders to an offscreen render target,
encodes frames as raw RGBA or compressed PNG, and streams them over the pipe.
Less efficient but works anywhere without `SetParent`. The embedded approach is
preferred on Windows.

### IPC protocol

All messages are newline-delimited JSON over a local named pipe. The pipe name
is passed as a command-line argument when the editor spawns the host.

**Editor → Host (commands):**

```json
{ "cmd": "set_mode",       "mode": "ui_preview" }
{ "cmd": "load_scene",     "path": "assets/scenes/town.arcscene" }
{ "cmd": "load_ui",        "path": "assets/engine/ui/pause_menu.arcui" }
{ "cmd": "set_widget_prop","name": "resume-btn", "prop": "size", "value": [300, 44] }
{ "cmd": "reload_asset",   "path": "assets/engine/ui/pause_menu.arcui" }
{ "cmd": "set_tile",       "layer": 0, "x": 5, "y": 3, "tile_id": 12 }
{ "cmd": "select_entity",  "entity_id": 1042 }
{ "cmd": "set_component",  "entity_id": 1042, "component": "Transform2D",
                            "field": "Position", "value": [128.0, 256.0] }
{ "cmd": "resize",         "width": 1024, "height": 600 }
{ "cmd": "shutdown" }
```

**Host → Editor (events):**

```json
{ "event": "log",          "channel": "Engine", "level": "info", "msg": "..." }
{ "event": "bus_event",    "name": "Scene.Loaded", "payload": { ... } }
{ "event": "entity_list",  "entities": [ { "id": 1042, "name": "Player", ... } ] }
{ "event": "ready" }
```

The protocol is versioned (`"version": 1` on the first message in each
direction) so the editor can refuse to connect to a mismatched host.

---

## Editor-side responsibilities

The C# editor never renders game content itself. It owns:

- **Property panels**: component fields, tile properties, widget properties — typed Avalonia controls bound to host-provided data
- **Asset browser**: file tree, thumbnail generation, drag-to-palette import
- **Node canvases**: dialog graph editor, event sequence timeline — pure AvaloniaUI with no engine rendering
- **Tile palette**: icon grid loaded from exported sprite sheet metadata; sends `set_tile` commands on click
- **Log panel**: filtered stream of log events from the host pipe
- **Event monitor**: filtered stream of bus events from the host pipe

---

## Implementation Phases

### Phase 40A — Editor shell & host integration (start here)

- [ ] C# AvaloniaUI solution (`editor/`), separate from the CMake build
- [ ] `arcbit-editor-host` CMake target (Phase 38 runtime, stripped to no game code, named-pipe IPC loop)
- [ ] Editor spawns host on startup; tears it down on close
- [ ] Named pipe connection with version handshake
- [ ] `NativeControlHost` panel embedding the host SDL window (Windows; fallback: textured frame stream)
- [ ] Host responds to `resize` commands
- [ ] Menu bar, dockable panel shell (no content yet)
- [ ] Engine log panel wired to host log events

### Phase 40B — Asset browser & importer UI

- [ ] File tree view rooted at the open project directory
- [ ] Asset type icons (texture, audio, font, arcui, scene, etc.)
- [ ] Double-click: opens asset in the relevant editor panel (scene → scene editor, arcui → UI designer, etc.)
- [ ] Drag source file onto browser: invokes `arcbit-pack`, shows import result inline
- [ ] Asset importer dialog: source path, target path, import options (e.g. sampler filter for textures)
- [ ] Thumbnail generation: textures get a SkiaSharp-decoded preview; other types get type icons

### Phase 40C — Tilemap editor

- [ ] Send `set_mode scene` + `load_scene` to host; host renders the tilemap in the embedded window
- [ ] Tile palette panel (C#): grid of tile icons from TileAtlas metadata; click to select active tile
- [ ] Layer panel: list of tilemap layers, visibility toggles, active-layer selector
- [ ] Paint / erase / fill tools: editor tracks pointer state; sends `set_tile` commands per tile touched
- [ ] Tile property editor: solid, light-blocking, flip-book animation, custom properties
- [ ] Undo/redo stack (client-side; replays inverse `set_tile` commands)
- [ ] Export: serialize the in-memory tile grid to the scene file format (JSON or `.arcasset`)

### Phase 40D — Scene editor

- [ ] Entity list panel: shows all entities with Name/Tag; click to select
- [ ] Inspector panel: displays all components on the selected entity; editable fields send `set_component` commands
- [ ] Entity placement tool: drag an entity prefab from the asset browser onto the canvas; sends `spawn_entity`
- [ ] Transform gizmos: the host draws debug overlays (AABB, position marker) for the selected entity
- [ ] NPC placement: script file assignment in inspector
- [ ] Patrol path editor: click to add waypoints; drawn by host as debug lines
- [ ] Scene save: serialize all entity positions + components back to the scene file

### Phase 40E — UI layout designer

- [ ] Send `set_mode ui_preview` + `load_ui` to host; host pushes the screen and renders it
- [ ] Widget tree panel: shows the widget hierarchy; click to select
- [ ] Inspector panel: widget properties (size, anchor, pivot, offset, colors, text) → sends `set_widget_prop`
- [ ] Skin override panel: per-widget `UISkinOverride` fields
- [ ] Live edits reflected immediately in the host viewport (no save required to preview)
- [ ] Save: write the modified widget tree back to the `.arcui` file
- [ ] Drag-to-add: drag a widget type from a palette onto the tree; sends `add_widget`
- [ ] Font registry panel: register `FontAtlas` instances under string keys; calls `UILoader::RegisterFont` at runtime

### Phase 40F — Dialog graph editor

- [ ] Node canvas (AvaloniaUI custom control): pan, zoom, connect nodes with bezier edges
- [ ] Node types: `SpeakNode`, `ChoiceNode`, `ConditionNode`, `ActionNode`
- [ ] Property panel for the selected node: text (with localization key picker), portrait, speaker
- [ ] Export to Phase 27 JSON dialog graph format
- [ ] Live preview: `set_mode ui_preview` + push a `DialogScreen` driven by the current graph

### Phase 40G — Event sequence editor (cutscene timeline)

- [ ] Timeline panel: horizontal track per command type; drag handles to set start time
- [ ] Command palette: click to add a command to the timeline at the playhead
- [ ] Per-command inspector: fill in parameters (camera target, sound key, dialog graph, etc.)
- [ ] Playback controls: play / pause / scrub sends `seek_sequence` commands to host
- [ ] Export to Phase 35 JSON sequence format

### Phase 40H — Live integration & event monitor

- [ ] Event monitor panel: live stream of bus events from the host; filterable by event name; shows payload
- [ ] **Run button**: triggers a CMake build, then launches the full game (not the editor host); optional direct-to-scene launch
- [ ] Scene launch selector: choose any registered scene as the start point before hitting Run
- [ ] Localization tools: key picker autocomplete in text fields, `en.json` export, per-locale coverage report
- [ ] Particle emitter preview panel: `set_mode particle`, spawn emitter; adjust parameters live
- [ ] Light animator curve editor: sends curve data to host; host applies to a `LightAnimator` component

---

## Phase 36 dependency — asset pipeline

The editor calls `arcbit-pack` (CLI) for all import operations. It does not link
against the engine DLL directly for asset handling — the content tool is the
shared boundary (Phase 36 `arcbit-content` DLL consumed by both the runtime and
the editor's C# bindings via P/Invoke or a thin C wrapper).

Editor mode detection (from `project.arcbit`):
- **Dev key present** → full editor mode
- **Mod key only, `mod_support = true`** → restricted mod editor mode; "Mod Editor" banner; save-over-original disabled

---

## File structure

```
editor/
  ArcbitEditor.sln
  ArcbitEditor/               — main AvaloniaUI project (C#)
    App.axaml
    MainWindow.axaml
    Views/
      SceneEditorView.axaml
      UIDesignerView.axaml
      TilemapEditorView.axaml
      DialogGraphView.axaml
      SequenceEditorView.axaml
      AssetBrowserView.axaml
      LogPanelView.axaml
      EventMonitorView.axaml
    ViewModels/
    Controls/
      NativeHostControl.cs    — NativeControlHost wrapper for embedded engine window
      NodeCanvas.cs           — base for dialog graph + sequence timeline canvases
    IPC/
      EditorHostClient.cs     — named pipe client, message serialization
      HostCommands.cs         — command record types
      HostEvents.cs           — event record types

engine/
  src/
    EditorHost.cpp            — named pipe server loop, command dispatch
  include/arcbit/editor/
    EditorHost.h
```
