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
- [ ] `CreateTexture` / `UploadTexture` / `DestroyTexture`
- [ ] `CreateSampler` / `DestroySampler`
- [ ] Descriptor pool + descriptor set management
- [ ] Textured quad demo

---

## Phase 8: Render Thread
- [ ] Dedicated render thread with double-buffered frame state
- [ ] Window resize → swapchain recreation (out-of-date handling)
- [ ] Input events forwarded from SDL thread to game thread

---

## Phase 9: 2D Deferred Lighting
- [ ] G-buffer render targets (albedo + normal)
- [ ] Geometry pass shaders
- [ ] Dynamic light SSBO (`BindStorageBuffer` — no fixed array limit)
- [ ] Light accumulation pass shaders
- [ ] Composite pass
- [ ] Point light demo scene

---

## Phase 10: Custom ECS
- [ ] Archetype storage (component pools grouped by component set)
- [ ] Entity create / destroy
- [ ] Query API (iterate entities matching a component mask)
- [ ] System registration + execution order
- [ ] Starter components: `Transform2D`, `Sprite`, `PointLight`

---

## Phase 11: Tilemap System
- [ ] Tile definition (ID, solid flag, custom properties)
- [ ] Chunk-based map storage (16×16 tile chunks)
- [ ] Tile atlas texture + UV lookup
- [ ] Camera / viewport transform
- [ ] Tilemap renderer (batched instanced quads)

---

## Phase 12: Lua Scripting
- [ ] Lua integration (sol2 via vcpkg)
- [ ] Script component (path to `.lua` file)
- [ ] NPC script hooks: `OnInteract`, `OnTick`, `OnCombatStart`
- [ ] ECS + inventory API exposed to Lua

---

## Phase 13: Inventory & Items
- [ ] Item definition (ID, name, stat block)
- [ ] Inventory component (item slots with quantities)
- [ ] Pick-up / drop / equip logic
- [ ] JSON save / load

---

## Phase 14: Turn-Based Combat
- [ ] Battle state machine (idle → select action → resolve → end check)
- [ ] Action queue (Pokémon-style turn order by speed)
- [ ] Move / stat system (HP, ATK, DEF, SPD, type)
- [ ] Combat UI (health bars, action menu, basic animations)

---

## Phase 15: C# AvaloniaUI Editor
- [ ] Separate solution / CMake-independent project
- [ ] Tilemap canvas (render tiles, layer management)
- [ ] Tile palette + placement / erase tools
- [ ] Tile property editor panel
- [ ] NPC placement + script file assignment
- [ ] Content export (binary format readable by the engine)
