# Arcbit Sprite Format — Specification v1.0

This is the single canonical format for all sprite sheet assets in Arcbit.
It covers named frames, uniform tile grids, and animation clips in one file.
The editor reads and writes this format; `arcbit-pack` (Phase 36) compiles it
into the binary `.arcasset` container for shipped builds.

---

## Full example

```json
{
  "version": "1.0",
  "texture": "player.png",
  "pixels_per_unit": 16,

  "frames": [
    { "name": "idle_down_0", "rect": { "x":  0, "y": 0, "w": 45, "h": 35 }, "pivot": { "x": 0.5, "y": 1.0 } },
    { "name": "idle_down_1", "rect": { "x": 45, "y": 0, "w": 45, "h": 35 }, "pivot": { "x": 0.5, "y": 1.0 } },
    { "name": "walk_down_0", "rect": { "x":  0, "y": 35, "w": 45, "h": 35 }, "pivot": { "x": 0.5, "y": 1.0 } },
    { "name": "walk_down_1", "rect": { "x": 45, "y": 35, "w": 45, "h": 35 }, "pivot": { "x": 0.5, "y": 1.0 } }
  ],

  "animations": [
    {
      "name": "idle_down",
      "loop": true,
      "frames": [
        { "frame": "idle_down_0", "duration_ms": 1600 },
        { "frame": "idle_down_1", "duration_ms": 60 }
      ]
    },
    {
      "name": "walk_down",
      "loop": true,
      "frames": [
        { "frame": "walk_down_0", "duration_ms": 120 },
        { "frame": "walk_down_1", "duration_ms": 120 }
      ]
    }
  ],

  "tile_grid": {
    "tile_width": 16,
    "tile_height": 16
  }
}
```

---

## Field reference

### `version` *(required)*

```json
"version": "1.0"
```

Format version string. The loader rejects files with an unrecognised version
rather than silently misreading them. Always `"1.0"` for files created now;
the minor component may be bumped for additive changes, the major component for
breaking changes.

---

### `texture` *(required)*

```json
"texture": "player.png"
```

Path to the PNG sprite sheet, relative to the JSON file's own directory. The
loader resolves it relative to the metadata file so both files can live in the
same folder.

---

### `pixels_per_unit` *(optional)*

```json
"pixels_per_unit": 16
```

How many texture pixels correspond to one world unit (tile). Used by the editor
to show sprites at their natural in-game scale in the preview viewport, and by
the engine to compute a default `Scale` when no explicit scale is set.

Omit if the project does not use a fixed pixels-per-unit convention.

---

### `frames` *(optional)*

An array of named rectangular regions of the texture. Each frame has a unique
name that animations and game code use to look it up.

```json
"frames": [
  {
    "name":  "idle_down_0",
    "rect":  { "x": 0, "y": 0, "w": 45, "h": 35 },
    "pivot": { "x": 0.5, "y": 1.0 }
  }
]
```

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Unique identifier for this frame within the file |
| `rect.x`, `rect.y` | yes | Top-left corner of the frame in texture pixels |
| `rect.w`, `rect.h` | yes | Width and height in texture pixels |
| `pivot.x`, `pivot.y` | no | Normalised origin point (0–1). Default `{ "x": 0.5, "y": 0.5 }` (centre) |

Common pivot values:
- `{ 0.5, 0.5 }` — centre (default)
- `{ 0.5, 1.0 }` — bottom-centre (standing characters)
- `{ 0.0, 0.0 }` — top-left

The engine uses the pivot to offset the sprite so its logical position matches
`Transform2D.Position`. A character with pivot `{ 0.5, 1.0 }` whose
Transform2D.Position is at tile centre will stand with its feet on the tile
rather than its centre floating above it.

---

### `animations` *(optional)*

Named sequences of frames with per-frame durations. Requires `frames` to be
defined in the same file (animation frames reference frame names, not indices).

```json
"animations": [
  {
    "name": "walk_down",
    "loop": true,
    "frames": [
      { "frame": "walk_down_0", "duration_ms": 120 },
      { "frame": "walk_down_1", "duration_ms": 120 }
    ]
  }
]
```

| Field | Required | Description |
|-------|----------|-------------|
| `name` | yes | Unique animation name used by the `Animator` component |
| `loop` | no | Whether the clip loops when it reaches the last frame. Default `false` |
| `frames[].frame` | yes | Name of a frame defined in the top-level `frames` array |
| `frames[].duration_ms` | yes | How long this frame is displayed, in milliseconds |

Animation frames reference frames **by name**, not by array index. This means
reordering the `frames` array never silently breaks animations.

---

### `tile_grid` *(optional)*

Defines a uniform grid layout for tileset sheets where every cell is the same
size. Tiles are indexed left-to-right, top-to-bottom starting at 0.

```json
"tile_grid": {
  "tile_width":  16,
  "tile_height": 16
}
```

| Field | Required | Description |
|-------|----------|-------------|
| `tile_width` | yes | Width of each tile in pixels |
| `tile_height` | yes | Height of each tile in pixels |

`tile_grid` and `frames` can coexist in the same file. A tileset might define
a grid for the bulk of its tiles and also list a few named frames for
special-purpose tiles that game code needs to look up by name.

---

## Which sections to include

A file may use any combination of the optional sections:

| Use case | Sections needed |
|----------|-----------------|
| Uniform tileset | `tile_grid` |
| Named sprite atlas (no animation) | `frames` |
| Animated character | `frames` + `animations` |
| Animated character on a tile grid | `tile_grid` + `frames` + `animations` |

---

## Carrot import

The Carrot engine uses a compatible sprite format. Arcbit can import it and
convert it to Arcbit native. Import is always **explicit** — the importer never
auto-detects or guesses the format, because Carrot files have no stable
identifying marker (the Carrot format may or may not gain a `version` field in
future, and absent fields are not a reliable signal).

Differences between Carrot format and Arcbit native:

| Aspect | Carrot | Arcbit native |
|--------|--------|---------------|
| Version field | not guaranteed | `"version": "1.0"` (required) |
| Animation frame reference | integer index into `frames` array | frame name string |
| Tile grid | not supported | `tile_grid` section |

The importer converts integer frame indices to the corresponding frame `name`
values and writes a versioned Arcbit native file. The original Carrot file is
not modified. If the source file does not conform to the expected Carrot
structure (missing required fields, wrong types, etc.) the import fails with a
clear error rather than producing a silently broken output.

Import paths:
- **Editor**: right-click a `.json` in the asset panel → *Import as Carrot format*
- **CLI**: `arcbit-pack --import-carrot source.json output.json`

Any `.json` without a `"version"` field that is opened as a plain Arcbit asset
(not via the explicit Carrot import path) is rejected with an error.
