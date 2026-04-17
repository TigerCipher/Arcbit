#pragma once

#include <arcbit/assets/AssetTypes.h>
#include <arcbit/render/RenderHandle.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace Arcbit
{

class TextureManager;

// ---------------------------------------------------------------------------
// SpriteSheet
//
// Associates a texture atlas with named or indexed UV rectangles.
// Loaded from a JSON metadata file; the referenced texture is loaded
// automatically via TextureManager.
//
// Two layout modes are supported — a single JSON file may use either or both:
//
//   Named sprites (e.g. character animations):
//   {
//     "texture": "assets/textures/player.png",
//     "sprites": [
//       { "name": "idle_0", "x": 0, "y": 0, "w": 48, "h": 48 },
//       { "name": "idle_1", "x": 48, "y": 0, "w": 48, "h": 48 }
//     ]
//   }
//
//   Tile grid (tileset sheets where every cell is the same size):
//   {
//     "texture": "assets/textures/grass_tileset.png",
//     "tile_width": 16,
//     "tile_height": 16
//   }
//   Tiles are indexed left-to-right, top-to-bottom starting at 0.
//
// Pixel coordinates in the JSON are converted to normalized (0-1) UVs using
// the loaded texture's dimensions, so the caller never needs raw pixel math.
// ---------------------------------------------------------------------------
class SpriteSheet
{
public:
    // Load a spritesheet from a JSON metadata file.
    // The texture path inside the JSON is resolved relative to metaPath's
    // directory, so the texture and JSON can live alongside each other.
    // Returns an invalid (empty) SpriteSheet on any error — check IsValid().
    [[nodiscard]] static SpriteSheet Load(std::string_view metaPath, TextureManager& textures);

    [[nodiscard]] bool          IsValid() const { return _texture.IsValid(); }
    [[nodiscard]] TextureHandle GetTexture() const { return _texture; }

    // Look up a named sprite. Returns std::nullopt if the name is not found.
    [[nodiscard]] std::optional<UVRect> GetSprite(std::string_view name) const;

    // Look up a tile by grid index (0-based, left-to-right, top-to-bottom).
    // Returns std::nullopt if the index is out of range or the sheet was
    // defined with named sprites only (no tile_width / tile_height).
    [[nodiscard]] std::optional<UVRect> GetTile(u32 index) const;

    // Look up a tile by 2D grid coordinates (column, row), both 0-based.
    // Equivalent to GetTile(y * columns + x).
    // Returns std::nullopt if the coordinates are out of range.
    [[nodiscard]] std::optional<UVRect> GetTile(u32 x, u32 y) const;

    // Total number of tiles in the grid (0 if not a tile-grid sheet).
    [[nodiscard]] u32 TileCount() const { return static_cast<u32>(_tiles.size()); }

    // Number of columns in the tile grid (0 if not a tile-grid sheet).
    [[nodiscard]] u32 TileColumns() const { return _columns; }

    // Number of rows in the tile grid (0 if not a tile-grid sheet).
    [[nodiscard]] u32 TileRows() const { return _columns > 0 ? static_cast<u32>(_tiles.size()) / _columns : 0; }

private:
    static void LoadNamedSpritesFromJson(std::string_view metaPath, const nlohmann::json& json, SpriteSheet& sheet, f32 invW,
                                         f32 invH);

    static void LoadFromTileGrid(std::string_view metaPath, const nlohmann::json& json, SpriteSheet& sheet, u32 width, u32 height,
                                 f32 invW, f32 invH);

private:
    TextureHandle                           _texture;
    std::unordered_map<std::string, UVRect> _namedSprites;
    std::vector<UVRect>                     _tiles;
    u32                                     _columns = 0; // tile grid columns; 0 if not a tile-grid sheet
};

} // namespace Arcbit
