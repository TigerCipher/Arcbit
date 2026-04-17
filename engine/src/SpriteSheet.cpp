#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace Arcbit
{

SpriteSheet SpriteSheet::Load(std::string_view metaPath, TextureManager& textures)
{
    std::ifstream file{ std::string(metaPath) };
    if (!file.is_open())
    {
        LOG_ERROR(Engine, "SpriteSheet: cannot open metadata file '{}'", metaPath);
        return {};
    }

    nlohmann::json json;
    try
    {
        file >> json;
    } catch (const nlohmann::json::exception& e)
    {
        LOG_ERROR(Engine, "SpriteSheet: JSON parse error in '{}': {}", metaPath, e.what());
        return {};
    }

    if (!json.contains("texture"))
    {
        LOG_ERROR(Engine, "SpriteSheet: missing 'texture' key in '{}'", metaPath);
        return {};
    }

    // Resolve the texture path relative to the metadata file's directory so
    // both the JSON and PNG can live in the same folder.
    const std::filesystem::path metaDir    = std::filesystem::path(metaPath).parent_path();
    const std::string           texRelPath = json["texture"].get<std::string>();
    const std::string           texPath    = (metaDir / texRelPath).lexically_normal().string();

    SpriteSheet sheet{};
    sheet._texture = textures.Load(texPath);
    if (!sheet._texture.IsValid())
        return {};

    const auto [Width, Height] = textures.GetInfo(sheet._texture);
    if (Width == 0 || Height == 0)
    {
        LOG_ERROR(Engine, "SpriteSheet: zero-dimension texture for '{}'", texPath);
        return {};
    }

    const f32 invW = 1.0f / static_cast<f32>(Width);
    const f32 invH = 1.0f / static_cast<f32>(Height);

    // --- Named sprites ---
    LoadNamedSpritesFromJson(metaPath, json, sheet, invW, invH);

    // --- Tile grid ---
    LoadFromTileGrid(metaPath, json, sheet, Width, Height, invW, invH);

    return sheet;
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

std::optional<UVRect> SpriteSheet::GetSprite(const std::string_view name) const
{
    const auto it = _namedSprites.find(std::string(name));
    if (it == _namedSprites.end())
        return std::nullopt;
    return it->second;
}

std::optional<UVRect> SpriteSheet::GetTile(const u32 index) const
{
    if (index >= _tiles.size())
        return std::nullopt;
    return _tiles[index];
}

std::optional<UVRect> SpriteSheet::GetTile(const u32 x, const u32 y) const
{
    if (_columns == 0 || x >= _columns)
        return std::nullopt;
    return GetTile(y * _columns + x);
}

void SpriteSheet::LoadNamedSpritesFromJson(std::string_view metaPath, const nlohmann::json& json, SpriteSheet& sheet,
                                           const f32 invW, const f32 invH)
{
    if (!json.contains("sprites"))
        return;
    for (const auto& s : json["sprites"])
    {
        const std::string name = s.at("name").get<std::string>();
        const f32         x    = s.at("x").get<f32>();
        const f32         y    = s.at("y").get<f32>();
        const f32         w    = s.at("w").get<f32>();
        const f32         h    = s.at("h").get<f32>();

        sheet._namedSprites[name] = UVRect{
            x * invW,
            y * invH,
            (x + w) * invW,
            (y + h) * invH,
        };
    }
    LOG_DEBUG(Engine, "SpriteSheet: loaded {} named sprites from '{}'", sheet._namedSprites.size(), metaPath);
}

void SpriteSheet::LoadFromTileGrid(std::string_view metaPath, const nlohmann::json& json, SpriteSheet& sheet,
                                   const u32 width, const u32 height, const f32 invW, const f32 invH)
{
    if (!json.contains("tile_width") || !json.contains("tile_height"))
        return;

    const u32 tileW   = json["tile_width"].get<u32>();
    const u32 tileH   = json["tile_height"].get<u32>();
    const u32 columns = width / tileW;
    const u32 rows    = height / tileH;

    if (columns == 0 || rows == 0)
    {
        LOG_WARN(Engine, "SpriteSheet: tile size {}x{} is larger than texture {}x{} in '{}'",
                 tileW, tileH, width, height, metaPath);
        return;
    }

    sheet._columns = columns;
    sheet._tiles.reserve(columns * rows);

    for (u32 row = 0; row < rows; ++row)
    {
        for (u32 col = 0; col < columns; ++col)
        {
            const auto px = static_cast<f32>(col * tileW);
            const auto py = static_cast<f32>(row * tileH);
            sheet._tiles.push_back(UVRect{
                px * invW,
                py * invH,
                (px + static_cast<f32>(tileW)) * invW,
                (py + static_cast<f32>(tileH)) * invH,
            });
        }
    }

    LOG_DEBUG(Engine, "SpriteSheet: built {}x{} tile grid ({} tiles) from '{}'",
              columns, rows, sheet._tiles.size(), metaPath);
}

} // namespace Arcbit