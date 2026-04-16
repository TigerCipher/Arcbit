# Arcbit — Project Blueprint

Checkbox list of every major milestone. Check off items as they are completed.

---

## Phase 0: Foundation
- [x] CMake project structure (root, core, engine, game, backends/vulkan)
- [x] vcpkg manifest + CMakePresets (SDL3, spdlog, VMA)

---

## Phase 1: Core Utilities
- [x] Type aliases (`i8` / `u8` / `f32` / `usize` / etc.)
- [x] Logger (spdlog, per-channel named loggers, DLL-safe SHARED library)
- [x] Assertions (`ARCBIT_ASSERT`, `ARCBIT_VERIFY`)
- [x] Profiler stub (`ScopedTimer`, Tracy-ready macros)

---

## Phase 2: Render API Design
- [x] `RenderHandle<Tag>` — generational indices, typed aliases
- [x] `RenderTypes` — enums, descriptors, flags system
- [x] `RenderDevice` public API (full surface, no Vulkan exposed)
- [x] `ARCBIT_RENDER_API` dllexport/dllimport macro

---

## Phase 3: Vulkan Backend — Initialisation
- [x] `VulkanContext` + `HandlePool<T>` slot-map
- [x] Instance creation + optional validation layers
- [x] Physical device selection (Vulkan 1.3 + dynamic rendering + sync2 required)
- [x] Logical device (dynamic rendering + synchronization2 features enabled)
- [x] VMA allocator
- [x] Command pool (RESET_COMMAND_BUFFER_BIT)

---

## Phase 4: Buffers & Shaders
- [x] `CreateBuffer` / `UpdateBuffer` / `DestroyBuffer`
- [x] `CreateShader` / `DestroyShader`

---

## Phase 5: Swapchain & Frame Loop
- [x] `CreateSwapchain` / `DestroySwapchain` / `ResizeSwapchain`
- [x] `AcquireNextImage` + per-frame sync (fences + semaphores)
- [x] `BeginCommandList` / `EndCommandList`
- [x] `BeginRendering` / `EndRendering` (image layout transitions via sync2)
- [x] `Submit` + `Present`
- [x] Clear-color frame rendered to the window in `main.cpp`

---

## Phase 6: Pipeline & Triangle
- [x] CMake glslc compile step (`.vert` / `.frag` → `.spv` at build time)
- [x] Minimal vertex + fragment shaders (hardcoded triangle, solid color)
- [x] `CreatePipeline` / `DestroyPipeline` (graphics pipeline)
- [x] `BindPipeline` / `SetViewport` / `SetScissor` / `Draw`
- [x] Triangle rendered to screen

---

## Phase 7: Textures & Samplers
- [x] `CreateTexture` / `UploadTexture` / `DestroyTexture`
- [x] `CreateSampler` / `DestroySampler`
- [x] Descriptor pool + descriptor set management
- [x] Textured quad demo

---

## Phase 8: Render Thread & Window
- [x] Dedicated render thread with double-buffered frame state
- [x] Window resize → swapchain recreation (out-of-date handling)
- [x] Raw SDL events forwarded from SDL thread to game thread via a lock-free queue
- [x] `Window` class — RAII SDL_Window wrapper; hides SDL from game/engine headers

---

## Phase 9: Input System
- [x] `Action` registry — map named actions (`"Move_Left"`, `"Interact"`) to one or more bindings
- [x] Binding types: keyboard key, mouse button, gamepad button, gamepad axis (with deadzone)
- [x] `InputManager::IsPressed(Action)` / `JustPressed` / `JustReleased` / `AxisValue`
- [x] Keyboard + mouse support via SDL3 event stream
- [x] Gamepad / controller support via SDL3 `SDL_Gamepad` API (hot-plug add/remove)
- [x] Multiple bindings per action (e.g. `"Interact"` → E key OR gamepad A button)
- [x] Runtime rebind API (used by the settings screen)

---

## Phase 10: Settings System
- [x] Settings file (JSON, loaded on startup / saved on shutdown or explicit flush)
- [x] Graphics settings: VSync, target resolution, fullscreen mode
- [x] Audio settings: master volume, music volume, SFX volume
- [x] Input settings: persist action→binding map from Phase 9; apply on load
- [x] `Settings::Get<T>` / `Set<T>` typed accessors with dirty-flag auto-save

---

## Phase 11: Application & Game Loop
- [x] `Application` class — owns Window, RenderThread, InputManager, SettingsSystem
- [x] `GameLoop` — fixed-timestep update tick separate from variable render tick
- [x] Virtual `OnStart` / `OnUpdate(f32 dt)` / `OnRender(FramePacket&)` / `OnShutdown` hooks
- [x] `main.cpp` reduced to constructing the Application subclass and calling `Run()`

---

## Phase 12: Asset System
- [x] `stb_image` integration (PNG / JPG decode to raw RGBA pixels)
- [x] `TextureManager` — load texture from file, cache by path, return `TextureHandle`
- [x] `SpriteSheet` — load atlas texture + JSON/binary metadata mapping sprite name/ID to UV rects
- [x] Asset hot-reload stub (watch file mtime, re-upload on change)

---

## Phase 13: 2D Forward+ Lighting
- [x] `UseNormalTexture` + `UseStorageBuffer` flags on `PipelineDesc`
- [x] `BindStorageBuffer` — bind SSBO to descriptor set (no fixed array limit)
- [x] `GetCurrentFrameIndex` — expose per-frame slot index for SSBO cycling
- [x] `forward.vert` / `forward.frag` — single-pass lit sprite shaders
- [x] Default flat-normal texture in `RenderThread` (fallback for unlit sprites)
- [x] Per-frame light SSBO cycling in `RenderThread`
- [x] `PointLight` struct + `Lights` / `AmbientColor` fields on `FramePacket`
- [x] Point light demo scene

---

## Phase 14: Sprite Batch Renderer
- [x] Per-frame CPU-side sprite buffer (position, UV rect, tint, layer)
- [x] Sort / group by texture to minimize descriptor switches
- [x] Single instance buffer upload + one `Draw` call per texture group
- [x] World-space coordinate system (pixels or tiles); push constant projects world → NDC

---

## Phase 15: Camera System
- [ ] `Camera2D` — world position, zoom level, rotation
- [ ] Smooth follow (lerp toward target entity, configurable lag)
- [ ] Viewport clamping (prevent camera from showing outside world bounds)
- [ ] Screen shake (trauma-based decay; add trauma on hit, explosion, etc.)
- [ ] World-to-screen and screen-to-world conversion helpers
- [ ] Camera push constant integrated into Sprite Batch and Tilemap renderers

---

## Phase 16: Custom ECS
- [ ] Archetype storage (component pools grouped by component set)
- [ ] Entity create / destroy
- [ ] Query API (iterate entities matching a component mask)
- [ ] System registration + execution order
- [ ] Starter components: `Transform2D`, `Sprite`, `PointLight`

---

## Phase 17: Animation System
- [ ] `AnimationClip` — ordered list of sprite frames with per-frame duration
- [ ] `Animator` component — current clip, playback state (playing / paused / stopped), loop flag
- [ ] State machine: named states, transitions triggered by conditions (e.g. `velocity > 0 → Walk`)
- [ ] Blend between clips (cross-fade for smoother transitions)
- [ ] Events on specific frames (e.g. `"FootStep"` on frame 2 — triggers audio / particle)
- [ ] Clips defined in JSON / loaded via asset system (Phase 12)

---

## Phase 18: Audio
- [ ] Audio library integration (miniaudio via vcpkg — single-header, no runtime deps)
- [ ] `AudioManager` — initialize device, master volume control
- [ ] Sound effect playback (fire-and-forget one-shots)
- [ ] Music streaming (looping background track, crossfade)
- [ ] `AudioSource` ECS component (spatial attenuation for point sounds)
- [ ] Wire volume levels to Settings System (Phase 10)

---

## Phase 19: Tilemap System
- [ ] Tile definition (ID, solid flag, light-blocking, interactable, custom properties)
- [ ] Chunk-based map storage (16×16 tile chunks)
- [ ] Tile atlas texture + UV lookup
- [ ] Tilemap renderer (batched instanced quads via Phase 14 sprite batcher)
- [ ] Camera / viewport culling (only submit visible chunks)
- [ ] Multiple layers (ground, objects, overlay; each layer rendered in order)

---

## Phase 20: Font Rendering
- [ ] Font rasterizer integration (`stb_truetype` or FreeType via vcpkg)
- [ ] Bitmap font atlas generation at startup (bake glyphs into a `TextureHandle`)
- [ ] `DrawText` helper — lays out a string into sprite-batch quads with correct UVs
- [ ] SDF font variant (optional — better quality at large sizes)

---

## Phase 21: GUI / UI System
- [ ] Retained-mode widget tree (`Panel`, `Label`, `Button`, `Image`, `ProgressBar`, `NineSlice`)
- [ ] Anchoring + relative layout (top-left, center, stretch, etc.)
- [ ] Input routing — UI consumes events before the game when a widget is focused
- [ ] Theming: font, colors, padding, border textures defined per skin file
- [ ] `Screen` abstraction — push / pop named screens (title, pause, HUD, dialog)
- [ ] HUD layer: health bars, minimap placeholder, action icons, status effects
- [ ] Input rebinding UI (reuses Phase 9 runtime rebind API)
- [ ] Splash screen system: ordered sequence of logo images shown at startup before the main menu; duration, fade-in/out, and skip-on-input configurable per entry in `project.arcbit`
- [ ] Editor integration: GUI layouts designed and previewed in the AvaloniaUI editor (Phase 38)

---

## Phase 22: 2D Physics
- [ ] Physics library integration (Box2D v3 via vcpkg)
- [ ] `PhysicsWorld` — fixed-timestep update, gravity config
- [ ] `Rigidbody2D` + `Collider2D` ECS components (AABB / circle)
- [ ] Collision event callbacks (enter / stay / exit) forwarded to ECS systems
- [ ] Debug draw overlay (wireframe collider shapes)

---

## Phase 23: Scene Management
- [ ] `Scene` class — owns an ECS world, tilemap, active lights, and camera
- [ ] `SceneManager` — load / unload / transition between scenes
- [ ] Async streaming: load next scene in background while current scene is still running
- [ ] Transition effects (fade-to-black, crossfade) using the GUI system (Phase 21)
- [ ] Scene stack for overlays (e.g. pop up an inventory screen over the world without unloading it)
- [ ] Entry-point scene configured in `project.arcbit` (Phase 35)

---

## Phase 24: Save / Load System
- [ ] `SaveSlot` — versioned binary or JSON snapshot of game state (separate from settings)
- [ ] Captured state: player position + scene, inventory, quest flags, NPC states, world mutations
- [ ] Multiple save slots with metadata (timestamp, playtime, thumbnail screenshot)
- [ ] Auto-save trigger API (on scene transition, on rest, on checkpoint)
- [ ] Migration / version-upgrade path so old saves stay compatible after content updates
- [ ] Lua API exposure so scripts can read and write custom save keys

---

## Phase 25: Lua Scripting
- [ ] Lua integration (sol2 via vcpkg)
- [ ] Script component (path to `.lua` file, loaded via asset system)
- [ ] Core lifecycle hooks: `OnStart`, `OnUpdate(dt)`, `OnDestroy`
- [ ] Entity hooks: `OnInteract`, `OnTick`, `OnCombatStart`, `OnCombatEnd`
- [ ] Engine API surface: ECS queries, entity create/destroy, component read/write
- [ ] Tilemap API: read/write tile properties, trigger map mutations
- [ ] Scene API: load scene, change entry point, spawn entities
- [ ] Save API: read/write custom save keys (Phase 24)
- [ ] Input API: query action state from scripts
- [ ] Audio API: play sound/music from scripts
- [ ] Sandboxed execution: scripts cannot access filesystem or network directly

---

## Phase 26: Dialog / Conversation System
*Default implementation — can be replaced by a Lua script if the game dev prefers a custom system.*

- [ ] `DialogGraph` — node-based conversation tree (speak, choice, condition, branch, action)
- [ ] Node types: `SpeakNode` (portrait, speaker name, text), `ChoiceNode` (up to N options), `ConditionNode` (check quest flag / inventory / stat), `ActionNode` (run Lua callback)
- [ ] `DialogManager` — queue and advance dialogs; integrates with GUI system (Phase 21) for the dialog box UI
- [ ] Interactable tile and NPC trigger: `OnInteract` starts a named dialog graph
- [ ] All displayed text referenced by localization key (Phase 27); raw strings are never embedded
- [ ] Variable substitution in text (`{player_name}`, `{gold_count}`, etc.) resolved via Localization interpolation
- [ ] Typewriter reveal effect with configurable speed; skip on confirm input
- [ ] Portraits / speaker icons per node (optional; falls back to name-only if absent)
- [ ] JSON authoring format; importable from editor (Phase 38)
- [ ] Lua hook: `OnDialogEnd(graphId, lastChoice)` for scripted reactions

---

## Phase 27: Localization
*English is required. All other languages are optional. The system is always active — it is the canonical way to define any player-visible text.*

- [ ] String key convention: all player-visible text in dialog, GUI, items, and combat is authored as a key (e.g. `"npc.elder.greeting"`) rather than a raw string
- [ ] Language resource files: one JSON file per locale (`en.json`, `ja.json`, etc.); keys map to translated strings
- [ ] `LocaleManager` — loads a locale file at startup (default: `en`); hot-swappable at runtime for in-game language selection
- [ ] `L("key")` / `Localize("key")` helper resolves a key to the active locale string; falls back to English if the key is missing in the target locale
- [ ] Fallback chain: requested locale → English → key string itself (so missing translations never crash or show blank)
- [ ] Plural forms: keys can define `_one` / `_other` variants (e.g. `"item.gold_one"` = "1 Gold", `"item.gold_other"` = "{count} Gold")
- [ ] String interpolation: `{player_name}`, `{count}`, `{item_name}` placeholders resolved at call site
- [ ] **Export pipeline**: editor scans all dialog graphs, GUI layouts, item definitions, and combat text; emits a canonical `en.json` with every key and its English value; translator-ready
- [ ] Language selection UI in settings screen (Phase 21); persisted in settings file (Phase 10)
- [ ] Editor integration (Phase 38): inline key picker, missing-key warnings, locale coverage report (% of keys translated per locale)

---

## Phase 28: Inventory & Items
- [ ] Item definition (ID, name, description, icon, stat block, type flags: consumable / equipment / key); all player-visible strings use localization keys (Phase 27)
- [ ] `Inventory` component — item slots with quantities; max-stack configurable per item type
- [ ] Pick-up / drop / equip / unequip logic; equip slots (weapon, armor, accessory, etc.)
- [ ] **Grid view**: icon-based inventory panel (like classic RPGs); drag-and-drop reorder
- [ ] **List view**: name + quantity rows (Pokémon-style); keyboard/gamepad navigable
- [ ] Both views built on the GUI system (Phase 21) and switchable at runtime
- [ ] Equipment stat application: `EquipmentSystem` aggregates equipped item bonuses onto a `Stats` component
- [ ] Loot tables: weighted random drops defined in JSON
- [ ] Save / load integration (Phase 24)
- [ ] Lua API: add/remove/check items from scripts

---

## Phase 29: AI & Pathfinding
- [ ] `NavGrid` — walkability grid derived from tilemap solid flags; rebuilt on map mutation
- [ ] A\* pathfinding with diagonal movement option; path smoothing (string-pulling)
- [ ] Steering behaviors: `Follow` (track target), `Wander` (random walk), `Flee` (move away from threat)
- [ ] `AIController` ECS component — configurable behavior tree or state machine
- [ ] Line-of-sight check (raytrace against solid tiles; used for perception radius)
- [ ] Patrol paths: series of waypoints defined per NPC in the editor
- [ ] Awareness states: `Idle → Alert → Chase → Combat` transitions
- [ ] Turn-based AI: action selection (attack / use item / move) driven by heuristics or Lua script
- [ ] Real-time AI: target acquisition, attack range check, cooldown management (used in Phase 31)
- [ ] Debug overlay: draw paths, awareness radius, LOS rays

---

## Phase 30: Turn-Based Combat
- [ ] Battle state machine (idle → select action → resolve → end check)
- [ ] Action queue (Pokémon-style turn order by speed stat)
- [ ] Move / stat system (HP, MP, ATK, DEF, SPD, type affinities)
- [ ] Status effects (poison, stun, burn, etc.) applied and ticked per turn
- [ ] Combat UI (health/MP bars, action menu, move list, enemy info panel); all text via localization keys (Phase 27)
- [ ] Transition in/out of battle (trigger from overworld collision or script)
- [ ] Basic combat animations (Phase 17 clip playback) for attack and hurt reactions
- [ ] Rewards: XP, gold, item drops via loot tables (Phase 28)
- [ ] Lua hooks: `OnBattleStart`, `OnTurnStart`, `OnActionResolved`, `OnBattleEnd`

---

## Phase 31: Live Combat
*Alternative to turn-based — toggled per encounter or globally by the game dev.*

- [ ] Real-time hitbox system: attack hitboxes active for specific animation frames (Phase 17 frame events)
- [ ] Melee combat: swing arc collider, hit detection, knockback impulse
- [ ] Ranged combat: projectile entity spawned at fire point, moves each tick, destroyed on hit or range
- [ ] Dodge / dash: brief i-frame window, directional impulse
- [ ] Enemy AI in real-time (Phase 29): target acquisition, attack wind-up telegraphing, retreat at low HP
- [ ] Combo system: input sequence maps to chained attacks (optional; data-driven per weapon type)
- [ ] Screen effects: hit-stop (brief time-scale reduction), screen flash, camera shake (Phase 15)
- [ ] Boss encounter flag: phase transitions at HP thresholds, scripted via Lua

---

## Phase 32: Particle System
- [ ] CPU-side particle pool: position, velocity, lifetime, color/alpha, scale — fixed-size ring buffer
- [ ] Emitter component: shape (point, line, circle, cone), burst vs. continuous mode, rate
- [ ] Per-particle modifiers: gravity, drag, color-over-lifetime, scale-over-lifetime
- [ ] Render: batched into sprite batcher as textured quads; sorted by layer
- [ ] Preset library: spark, smoke, blood splash, magic aura, footstep dust, rain, snow
- [ ] Lua API: spawn named emitter at world position, stop/pause emitter
- [ ] Editor integration: preview emitters in the editor canvas (Phase 38)

---

## Phase 33: Cutscene & Event System
- [ ] `EventSequence` — ordered list of timed commands (timeline-style)
- [ ] Command types: `MoveCameraTo`, `PanCamera`, `PlayAnimation`, `PlaySound`, `ShowDialog`, `FadeScreen`, `WaitSeconds`, `WaitForInput`, `SetFlag`, `RunLua`
- [ ] `EventTrigger` tile/entity property — starts a named sequence when entered or interacted with
- [ ] Sequence authoring in JSON; imported and previewed in editor (Phase 38)
- [ ] Skippable flag per sequence (skip jumps to end, fires all `SetFlag` / `RunLua` commands immediately)
- [ ] Supports both in-engine cutscenes and background story events (no camera override needed)

---

## Phase 34: Content Tools
*A shared `arcbit-content` DLL consumed by both the engine and the AvaloniaUI editor, ensuring identical read/write logic for all binary asset formats.*

- [ ] `.arcasset` binary container format: magic bytes, version, table of contents, compressed payload sections
- [ ] Supported asset types in v1: `Texture` (raw pixels + metadata), `SpriteSheet` (texture + UV table), `AnimationClip` (frame list + events), `TileAtlas` (texture + tile property table), `AudioClip` (decoded PCM + loop points)
- [ ] **Texture atlas packer**: bin individual sprites and spritesheets into power-of-two atlas pages grouped by sampler type (nearest / linear); output UV mapping table in `.arcasset` so game code still refers to sprites by name; `arcbit-pack` supports `--atlas` mode for batch packing
- [ ] Import pipeline: source file (PNG, JSON, WAV, etc.) → validated → packed into `.arcasset`
- [ ] Export pipeline: `.arcasset` → engine `TextureHandle` / `AudioClip` / etc. at runtime
- [ ] **Two-key asset protection system**:
  - *Dev key*: generated per project, stored in the dev's `project.arcbit` (never shipped with the game); baked into the game binary by the packaging step at build time; required to open `.arcasset` files in full editor mode
  - *Mod key*: a separate per-project key included in the shipped `project.arcbit` only when `mod_support = true`; used to sign assets produced by end users in mod editor mode; engine distinguishes mod assets from dev assets by key
  - When `mod_support = false`, shipped builds contain no key material; `.arcasset` files are opaque to end users (obfuscation, not hard encryption — raises the bar for casual extraction)
- [ ] **Mod editor mode** (activated when the editor opens a shipped `project.arcbit` with `mod_support = true`):
  - Open and preview original dev assets (decrypted in memory via the mod key where possible, or by asking the game runtime)
  - Extract audio and image assets to standard formats (WAV, PNG, etc.) for use as mod source material
  - "Save" is disabled on original assets — only "Save as Mod" is available, which always writes to the project's `mods/` directory signed with the mod key; original `.arcasset` files are never overwritten
- [ ] Hot-reload: engine watches `.arcasset` mtime and re-imports on change (extends Phase 12 stub)
- [ ] CLI tool (`arcbit-pack`) for batch import from a source assets directory

---

## Phase 35: Mod Support
*Optional — enabled per project in `project.arcbit`.*

- [ ] Mod discovery: scan a `mods/` folder for subdirectories, each containing a `mod.json` manifest
- [ ] `mod.json`: mod ID, display name, version, load order, list of Lua entry scripts
- [ ] Load order resolution: mods declare soft dependencies; engine topologically sorts them
- [ ] Lua mod API extensions (on top of Phase 25): `Entity.Spawn`, `Tilemap.SetTile`, `ItemRegistry.Register`, `DialogRegistry.Register`, `Event.Subscribe`
- [ ] Asset override: mod can shadow engine assets by placing an `.arcasset` at the same relative path; mod assets must be signed with the mod key (produced by the mod editor) — the engine rejects any file signed with the dev key that did not ship with the game
- [ ] Conflict detection: warn when two mods override the same asset or define the same item ID
- [ ] Mod can be disabled at runtime; save system tags which mods were active (warn on load if missing)
- [ ] Steam Workshop integration: wires into mod discovery when `steam_workshop = true` in `project.arcbit` (requires Phase 37)

---

## Phase 36: Data-Driven Runtime
- [ ] `project.arcbit` JSON project file format (title, resolution, entry scene, asset/script paths, mod support flag)
- [ ] `runtime/` CMake project — generic, pre-compiled Application host with no game-specific code
- [ ] Project file loader replaces hardcoded `ApplicationConfig` in the runtime path
- [ ] Lua hook wiring: `OnStart` loads entry scene, `OnUpdate` dispatches to Lua `OnTick`, etc.
- [ ] Defined export folder layout (`runtime.exe`, `project.arcbit`, `assets/`, `scenes/`, `scripts/`, `shaders/`, `mods/`)
- [ ] C++ subclass path (`game/`) remains fully supported alongside the data-driven path

---

## Phase 37: Steam / Platform Integration
*Optional — enabled per project. Requires a Steamworks SDK license (free for Steam distribution).*

- [ ] Steamworks SDK integration (dynamic DLL load; gracefully no-ops when Steam is not running)
- [ ] Steam initialization: `SteamAPI_Init` on startup, `SteamAPI_RunCallbacks` each tick, `SteamAPI_Shutdown` on exit
- [ ] Achievements: `AchievementManager` with named achievements defined in `project.arcbit`; unlock via `AchievementManager::Unlock("ACH_FIRST_BATTLE")`; synced to Steam backend
- [ ] Stats: integer / float stats (playtime, enemies defeated, etc.) submitted to Steam leaderboards
- [ ] Cloud saves: `SteamRemoteStorage` mirrors the save slot files from Phase 24; conflict resolution (newest wins or user prompt)
- [ ] Rich presence: set activity string shown in Steam friends list (e.g. "Exploring the Forest")
- [ ] Steam overlay: ensure `SDL_Window` is correctly set up for the overlay to render (Vulkan surface compatible)
- [ ] DLC flag system: `SteamApps::BIsDlcInstalled(appId)` gates content; checked at asset load time
- [ ] Steam Workshop: full discovery + download pipeline (extends the stub in Phase 35)
- [ ] Build pipeline: `steam_appid.txt` present in output directory; packaging script generates depot manifests

---

## Phase 38: C# AvaloniaUI Editor
- [ ] Separate solution / CMake-independent project
- [ ] Tilemap canvas (render tiles, layer management)
- [ ] Tile palette + placement / erase tools
- [ ] Tile property editor panel (solid, light-blocking, interactable, custom properties)
- [ ] NPC placement + script file assignment
- [ ] Dialog graph editor (node canvas for Phase 26 conversation trees)
- [ ] Event sequence editor (timeline view for Phase 33 cutscene commands)
- [ ] GUI layout designer (drag-and-drop widget canvas for Phase 21 screens)
- [ ] Particle emitter preview panel (live preview of Phase 32 emitter configs)
- [ ] Localization tools: key picker, `en.json` export, per-locale coverage report (Phase 27)
- [ ] **Run button**: builds the game and launches it in its own window; optionally opens directly to a selected scene (useful when iterating on a specific area)
- [ ] **Scene launch selector**: choose any registered scene as the start point before hitting Run
- [ ] **Engine log panel**: the running game pipes its spdlog output back to the editor over a named pipe or local socket; displayed in a filterable log panel in the editor
- [ ] Content export to Phase 36 folder layout (`project.arcbit` + `.arcasset` files via Phase 34 content tools)
- [ ] Asset importer UI: drag source files in, editor calls `arcbit-pack` and shows result
- [ ] **Editor mode detection**: on open, inspect the loaded `project.arcbit` to determine mode — full dev mode (dev key present) or restricted mod mode (mod key only, `mod_support = true`); mod mode disables save-over-original and shows a clear "Mod Editor" banner so end users know what they are working with
