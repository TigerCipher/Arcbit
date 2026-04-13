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
- [x] Minimal vertex + fragment shaders (hardcoded triangle, solid colour)
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

## Phase 8: Render Thread
- [x] Dedicated render thread with double-buffered frame state
- [x] Window resize → swapchain recreation (out-of-date handling)
- [x] Raw SDL events forwarded from SDL thread to game thread via a lock-free queue

---

## Phase 9: Input System
- [ ] `Action` registry — map named actions (`"Move_Left"`, `"Interact"`) to one or more bindings
- [ ] Binding types: keyboard key, mouse button, gamepad button, gamepad axis (with deadzone)
- [ ] `InputManager::IsPressed(Action)` / `JustPressed` / `JustReleased` / `AxisValue`
- [ ] Keyboard + mouse support via SDL3 event stream
- [ ] Gamepad / controller support via SDL3 `SDL_Gamepad` API (hot-plug add/remove)
- [ ] Multiple bindings per action (e.g. `"Interact"` → E key OR gamepad A button)
- [ ] Runtime rebind API (used by the settings screen)

---

## Phase 10: Settings System
- [ ] Settings file (JSON, loaded on startup / saved on shutdown or explicit flush)
- [ ] Graphics settings: VSync, target resolution, fullscreen mode
- [ ] Audio settings: master volume, music volume, SFX volume
- [ ] Input settings: persist action→binding map from Phase 9; apply on load
- [ ] `Settings::Get<T>` / `Set<T>` typed accessors with dirty-flag auto-save

---

## Phase 11: Asset System
- [ ] `stb_image` integration (PNG / JPG decode to raw RGBA pixels)
- [ ] `TextureManager` — load texture from file, cache by path, return `TextureHandle`
- [ ] `SpriteSheet` — load atlas texture + JSON/binary metadata mapping sprite name/ID to UV rects
- [ ] Asset hot-reload stub (watch file mtime, re-upload on change)

---

## Phase 12: 2D Deferred Lighting
- [ ] G-buffer render targets (albedo + normal)
- [ ] Geometry pass shaders
- [ ] Dynamic light SSBO (`BindStorageBuffer` — no fixed array limit)
- [ ] Light accumulation pass shaders
- [ ] Composite pass
- [ ] Point light demo scene

---

## Phase 13: Sprite Batch Renderer
- [ ] Per-frame CPU-side sprite buffer (position, UV rect, tint, layer)
- [ ] Sort / group by texture to minimise descriptor switches
- [ ] Single instance buffer upload + one `Draw` call per texture group
- [ ] Camera / viewport transform (world → NDC matrix via push constant)

---

## Phase 14: Custom ECS
- [ ] Archetype storage (component pools grouped by component set)
- [ ] Entity create / destroy
- [ ] Query API (iterate entities matching a component mask)
- [ ] System registration + execution order
- [ ] Starter components: `Transform2D`, `Sprite`, `PointLight`

---

## Phase 15: Audio
- [ ] Audio library integration (miniaudio via vcpkg — single-header, no runtime deps)
- [ ] `AudioManager` — initialise device, master volume control
- [ ] Sound effect playback (fire-and-forget one-shots)
- [ ] Music streaming (looping background track, crossfade)
- [ ] `AudioSource` ECS component (spatial attenuation for point sounds)
- [ ] Wire volume levels to Settings System (Phase 10)

---

## Phase 16: Tilemap System
- [ ] Tile definition (ID, solid flag, custom properties)
- [ ] Chunk-based map storage (16×16 tile chunks)
- [ ] Tile atlas texture + UV lookup
- [ ] Tilemap renderer (batched instanced quads via Phase 13 sprite batcher)
- [ ] Camera / viewport culling (only submit visible chunks)

---

## Phase 17: Font Rendering
- [ ] Font rasteriser integration (`stb_truetype` or FreeType via vcpkg)
- [ ] Bitmap font atlas generation at startup (bake glyphs into a `TextureHandle`)
- [ ] `DrawText` helper — lays out a string into sprite-batch quads with correct UVs
- [ ] SDF font variant (optional — better quality at large sizes)

---

## Phase 18: 2D Physics
- [ ] Physics library integration (Box2D v3 via vcpkg)
- [ ] `PhysicsWorld` — fixed-timestep update, gravity config
- [ ] `Rigidbody2D` + `Collider2D` ECS components (AABB / circle)
- [ ] Collision event callbacks (enter / stay / exit) forwarded to ECS systems
- [ ] Debug draw overlay (wireframe collider shapes)

---

## Phase 19: Lua Scripting
- [ ] Lua integration (sol2 via vcpkg)
- [ ] Script component (path to `.lua` file)
- [ ] NPC script hooks: `OnInteract`, `OnTick`, `OnCombatStart`
- [ ] ECS + inventory API exposed to Lua

---

## Phase 20: Inventory & Items
- [ ] Item definition (ID, name, stat block)
- [ ] Inventory component (item slots with quantities)
- [ ] Pick-up / drop / equip logic
- [ ] JSON save / load

---

## Phase 21: Turn-Based Combat
- [ ] Battle state machine (idle → select action → resolve → end check)
- [ ] Action queue (Pokémon-style turn order by speed)
- [ ] Move / stat system (HP, ATK, DEF, SPD, type)
- [ ] Combat UI (health bars, action menu, basic animations)

---

## Phase 22: C# AvaloniaUI Editor
- [ ] Separate solution / CMake-independent project
- [ ] Tilemap canvas (render tiles, layer management)
- [ ] Tile palette + placement / erase tools
- [ ] Tile property editor panel
- [ ] NPC placement + script file assignment
- [ ] Content export (binary format readable by the engine)
