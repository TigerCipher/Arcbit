#include <arcbit/tilemap/TileAtlas.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/core/Log.h>

namespace Arcbit {

bool TileAtlas::Load(std::string_view texturePath, const u32 tilePixelW, const u32 tilePixelH,
                     TextureManager& textures)
{
    _texture = textures.Load(texturePath);
    if (!_texture.IsValid())
    {
        LOG_WARN(Engine, "TileAtlas: failed to load texture '{}'", texturePath);
        return false;
    }

    const TextureInfo info = textures.GetInfo(_texture);
    if (info.Width == 0 || info.Height == 0 || tilePixelW == 0 || tilePixelH == 0)
    {
        LOG_WARN(Engine, "TileAtlas: invalid dimensions for '{}'", texturePath);
        return false;
    }

    _columns       = info.Width  / tilePixelW;
    _rows          = info.Height / tilePixelH;
    _invTexWidth   = 1.0f / static_cast<f32>(info.Width);
    _invTexHeight  = 1.0f / static_cast<f32>(info.Height);
    _tilePixelSize = { static_cast<f32>(tilePixelW), static_cast<f32>(tilePixelH) };

    LOG_INFO(Engine, "TileAtlas: loaded '{}' - {}x{} tiles ({}x{} px each)",
             texturePath, _columns, _rows, tilePixelW, tilePixelH);
    return true;
}

UVRect TileAtlas::GetUV(const u32 col, const u32 row) const
{
    const f32 u0 = static_cast<f32>(col)     * _tilePixelSize.X * _invTexWidth;
    const f32 v0 = static_cast<f32>(row)     * _tilePixelSize.Y * _invTexHeight;
    const f32 u1 = static_cast<f32>(col + 1) * _tilePixelSize.X * _invTexWidth;
    const f32 v1 = static_cast<f32>(row + 1) * _tilePixelSize.Y * _invTexHeight;
    return { u0, v0, u1, v1 };
}

} // namespace Arcbit
