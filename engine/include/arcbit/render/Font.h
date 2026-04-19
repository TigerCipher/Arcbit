#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>
#include <arcbit/assets/AssetTypes.h>

#include <string_view>
#include <unordered_map>

namespace Arcbit {

struct FramePacket;
class  RenderDevice;

enum class FontMode
{
    // RGBA8 atlas; white pixels with alpha = coverage.
    // DrawText pushes into packet.Sprites — world-space, camera-affected, lit.
    Bitmap,

    // R-channel signed distance field atlas (stb onedge=128, dist_scale=64).
    // DrawText pushes into packet.SDFSprites — screen-space pixels, no lighting.
    SDF,
};

// Per-glyph layout metrics produced by the atlas baker.
struct GlyphInfo
{
    UVRect UV;          // normalized sub-rect in the atlas texture
    Vec2   BitmapSize;  // bitmap width × height in pixels (includes SDF padding for SDF mode)
    Vec2   Bearing;     // (xoff, yoff) from cursor/baseline to bitmap top-left (stb convention)
    f32    Advance;     // horizontal advance in pixels at the baked pixelSize
};

// Bakes a TTF font into a GPU texture atlas at startup.
// ASCII 32–126 is supported. Load() is called once; the atlas is immutable thereafter.
class FontAtlas
{
public:
    // Bake a TTF file at pixelSize in the chosen mode. Returns false on load failure.
    bool Load(std::string_view ttfPath, f32 pixelSize, FontMode mode, RenderDevice& device);

    [[nodiscard]] const GlyphInfo* GetGlyph(u32 codepoint) const;
    [[nodiscard]] TextureHandle    GetTexture()    const { return _texture; }
    [[nodiscard]] SamplerHandle    GetSampler()    const { return _sampler; }
    [[nodiscard]] FontMode         GetMode()       const { return _mode; }
    [[nodiscard]] f32              GetLineHeight() const { return _lineHeight; }
    [[nodiscard]] f32              GetAscent()     const { return _ascent; }
    [[nodiscard]] bool             IsValid()       const { return _texture.IsValid(); }

private:
    std::unordered_map<u32, GlyphInfo> _glyphs;
    TextureHandle _texture;
    SamplerHandle _sampler;
    FontMode      _mode       = FontMode::Bitmap;
    f32           _lineHeight = 0.0f;
    f32           _ascent     = 0.0f;
};

enum class TextAlign
{
    Left,    // position.X is the left edge of each line (default)
    Center,  // position.X is the horizontal center of each line
    Right,   // position.X is the right edge of each line
};

// Submit a string as textured quads into the frame packet.
//
// Bitmap FontAtlas: position is a world-space coordinate; quads land in
//   packet.Sprites and are affected by the camera and point lighting.
//
// SDF FontAtlas: position is a screen-space coordinate in pixels measured
//   from the window's top-left corner; quads land in packet.SDFSprites and
//   are rendered without camera transform or lighting — ideal for HUD and
//   debug overlays.
//
// scale multiplies the baked pixelSize: 1.0 = original size, 2.0 = doubled.
// align controls horizontal alignment relative to position.X — Left keeps
//   the default behavior; Center/Right shift each line so it anchors at
//   position.X rather than starting from it.
// layer controls draw order within the same packet list.
void DrawText(FramePacket& packet, const FontAtlas& font, std::string_view text,
              Vec2 position, f32 scale, Color color,
              i32 layer = 0, TextAlign align = TextAlign::Left);

// Same as DrawText but emits into packet.UISprites with SDFMode=true so the
// UI pipeline renders it at the correct layer relative to UI backgrounds.
// Used by Label and Button widgets — do not call from world-space game code.
void DrawTextUI(FramePacket& packet, const FontAtlas& font, std::string_view text,
                Vec2 position, f32 scale, Color color,
                i32 layer = 0, TextAlign align = TextAlign::Left);

} // namespace Arcbit
