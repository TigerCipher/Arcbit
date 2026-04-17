### Proposed JSON format (v1)

Below is a simple and practical structure that should work well for most engines:

```json
{
  "texture": "player.png",

  "frames": [
    {
      "name": "idle_down_0",
      "rect": { "x": 0, "y": 0, "w": 45, "h": 35 },
      "pivot": { "x": 0.5, "y": 0.5 }
    },
    {
      "name": "idle_down_1",
      "rect": { "x": 45, "y": 0, "w": 45, "h": 35 },
      "pivot": { "x": 0.5, "y": 0.5 }
    }
  ],

  "animations": [
    {
      "name": "idle_down",
      "loop": true,
      "frames": [
        { "frame": 0, "duration_ms": 1600 },
        { "frame": 1, "duration_ms": 60 }
      ]
    }
  ],

  "pixels_per_unit": 16
}
```

---

### Field explanations

#### `texture`

* The filename of the exported PNG sprite sheet

#### `frames`

Each frame represents a region of the sprite sheet:

* `name` — unique identifier for the frame
* `rect` — position and size inside the PNG:

  * `x`, `y` = top-left corner
  * `w`, `h` = width and height in pixels
* `pivot` (optional) — the origin point for the sprite (explained below)

---

### What is a pivot?

The pivot (or origin) is the point inside the sprite that is used as its position in the game.

For example:

* `{ "x": 0.5, "y": 0.5 }` → center of the sprite
* `{ "x": 0.5, "y": 1.0 }` → bottom-center (very common for characters standing on the ground)
* `{ "x": 0.0, "y": 0.0 }` → top-left corner

The values are normalized:

* `0.0` = start of the frame (left or top)
* `1.0` = end of the frame (right or bottom)

If omitted, engines typically assume a default (usually center).

---

### `animations`

Animations are named sequences of frames:

* `name` — animation name
* `loop` — whether it repeats
* `frames` — ordered list of frames

Each animation frame contains:

* `frame` — index into the `frames` array
* `duration_ms` — how long that frame is displayed

---

### Optional fields

* `pivot` can be omitted per-frame if a default is preferred
* `pixels_per_unit` is optional and mainly used for scaling in some engines

---

### Notes

* This format intentionally avoids complexity and should be straightforward to generate
* It maps closely to how most engines represent sprite animations internally
* I’m happy to adjust details if something here is difficult to support on your side