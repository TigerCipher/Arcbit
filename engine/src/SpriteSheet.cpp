#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace Arcbit {

SpriteSheet SpriteSheet::Load(std::string_view metaPath, TextureManager& textures)
{
    std::ifstream file{ std::string(metaPath) };
    if (!file.is_open()) {
        LOG_ERROR(Engine, "SpriteSheet: cannot open metadata file '{}'", metaPath);
        return {};
    }

    nlohmann::json json;
    try {
        file >> json;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Engine, "SpriteSheet: JSON parse error in '{}': {}", metaPath, e.what());
        return {};
    }

    if (!json.contains("texture")) {
        LOG_ERROR(Engine, "SpriteSheet: missing 'texture' key in '{}'", metaPath);
        return {};
    }

    // Resolve the texture path relative to the metadata file's directory so
    // both the JSON and PNG can live in the same folder.
    const std::filesystem::path metaDir = std::filesystem::path(metaPath).parent_path();
    const std::string texRelPath        = json["texture"].get<std::string>();
    const std::string texPath           = (metaDir / texRelPath).lexically_normal().string();

    SpriteSheet sheet{};
    sheet.m_Texture = textures.Load(texPath);
    if (!sheet.m_Texture.IsValid())
        return {};

    const TextureInfo info = textures.GetInfo(sheet.m_Texture);
    if (info.Width == 0 || info.Height == 0) {
        LOG_ERROR(Engine, "SpriteSheet: zero-dimension texture for '{}'", texPath);
        return {};
    }

    const float invW = 1.0f / static_cast<float>(info.Width);
    const float invH = 1.0f / static_cast<float>(info.Height);

    // --- Named sprites ---
    if (json.contains("sprites")) {
        for (const auto& s : json["sprites"]) {
            const std::string name = s.at("name").get<std::string>();
            const float x = s.at("x").get<float>();
            const float y = s.at("y").get<float>();
            const float w = s.at("w").get<float>();
            const float h = s.at("h").get<float>();

            sheet.m_NamedSprites[name] = UVRect{
                x * invW,
                y * invH,
                (x + w) * invW,
                (y + h) * invH,
            };
        }
        LOG_DEBUG(Engine, "SpriteSheet: loaded {} named sprites from '{}'",
                  sheet.m_NamedSprites.size(), metaPath);
    }

    // --- Tile grid ---
    if (json.contains("tile_width") && json.contains("tile_height")) {
        const u32 tileW   = json["tile_width"].get<u32>();
        const u32 tileH   = json["tile_height"].get<u32>();
        const u32 columns = info.Width  / tileW;
        const u32 rows    = info.Height / tileH;

        if (columns == 0 || rows == 0) {
            LOG_WARN(Engine, "SpriteSheet: tile size {}x{} is larger than texture {}x{} in '{}'",
                     tileW, tileH, info.Width, info.Height, metaPath);
        } else {
            sheet.m_Tiles.reserve(columns * rows);
            for (u32 row = 0; row < rows; ++row) {
                for (u32 col = 0; col < columns; ++col) {
                    const float px = static_cast<float>(col * tileW);
                    const float py = static_cast<float>(row * tileH);
                    sheet.m_Tiles.push_back(UVRect{
                        px                    * invW,
                        py                    * invH,
                        (px + tileW)          * invW,
                        (py + tileH)          * invH,
                    });
                }
            }
            LOG_DEBUG(Engine, "SpriteSheet: built {}x{} tile grid ({} tiles) from '{}'",
                      columns, rows, sheet.m_Tiles.size(), metaPath);
        }
    }

    return sheet;
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

std::optional<UVRect> SpriteSheet::GetSprite(std::string_view name) const
{
    const auto it = m_NamedSprites.find(std::string(name));
    if (it == m_NamedSprites.end())
        return std::nullopt;
    return it->second;
}

std::optional<UVRect> SpriteSheet::GetTile(u32 index) const
{
    if (index >= m_Tiles.size())
        return std::nullopt;
    return m_Tiles[index];
}

} // namespace Arcbit
