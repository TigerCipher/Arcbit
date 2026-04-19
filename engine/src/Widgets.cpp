#include <arcbit/ui/Widgets.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/Font.h>

#include <algorithm>
#include <string>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static i32 BgLayer(const UIWidget& w) { return UIBaseLayer + w.ZOrder * UILayerStride + UILayerBg; }

static i32 FillLayer(const UIWidget& w) { return UIBaseLayer + w.ZOrder * UILayerStride + UILayerFill; }

static i32 TextLayer(const UIWidget& w) { return UIBaseLayer + w.ZOrder * UILayerStride + UILayerText; }

// Push a solid-color quad into UISprites.
static void PushRect(FramePacket&        packet, const UIRect&    r, const Color& c,
                     const TextureHandle tex, const SamplerHandle samp, const i32 layer)
{
    Sprite s{};
    s.Texture  = tex;
    s.Sampler  = samp;
    s.Position = {r.X + r.W * 0.5f, r.Y + r.H * 0.5f};
    s.Size     = {r.W, r.H};
    s.Tint     = c;
    s.Layer    = layer;
    packet.UISprites.push_back(s);
}

// Apply effective opacity to a color.
static Color WithAlpha(Color c, const f32 alpha) { c.A *= alpha; return c; }

// Push a textured quad (arbitrary UV) into UISprites.
static void PushTexturedRect(FramePacket& packet, const UIRect& r, const UVRect& uv,
                              const Color& c, TextureHandle tex, SamplerHandle samp, i32 layer)
{
    if (r.W <= 0.0f || r.H <= 0.0f) return;
    Sprite s{};
    s.Texture  = tex;
    s.Sampler  = samp;
    s.Position = { r.X + r.W * 0.5f, r.Y + r.H * 0.5f };
    s.Size     = { r.W, r.H };
    s.UV       = uv;
    s.Tint     = c;
    s.Layer    = layer;
    packet.UISprites.push_back(s);
}

// Word-wrap text to fit within maxWidth pixels, preserving explicit newlines.
static std::string WrapText(const FontAtlas& font, const std::string_view text,
                             const f32 maxWidth, const f32 scale)
{
    const GlyphInfo* spGlyph = font.GetGlyph(' ');
    const f32        spaceW  = spGlyph ? spGlyph->Advance * scale : 0.0f;

    std::string      result;
    std::string_view remaining = text;

    while (true) {
        const auto       nl   = remaining.find('\n');
        std::string_view para = remaining.substr(0, nl);
        if (!result.empty()) result += '\n';

        f32  lineWidth = 0.0f;
        bool lineStart = true;
        while (!para.empty()) {
            const auto       sp    = para.find(' ');
            std::string_view word  = para.substr(0, sp);
            const f32        wordW = MeasureTextWidth(font, word, scale);
            const f32        addW  = lineStart ? wordW : spaceW + wordW;

            if (!lineStart && lineWidth + addW > maxWidth) {
                result    += '\n';
                lineWidth  = 0.0f;
                lineStart  = true;
            }
            if (!lineStart) { result += ' '; lineWidth += spaceW; }
            result    += std::string(word);
            lineWidth += wordW;
            lineStart  = false;

            if (sp == std::string_view::npos) break;
            para = para.substr(sp + 1);
        }

        if (nl == std::string_view::npos) break;
        remaining = remaining.substr(nl + 1);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

void Panel::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                      const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                      const UISkin&       skin)
{
    PushRect(packet, myRect, WithAlpha(skin.PanelBg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this));

    if (DrawBorder) {
        // Simple 1px border — four thin rects around the edge.
        const Color bc = WithAlpha(skin.PanelBorder, effectiveOpacity);
        const f32   t  = 1.0f;
        PushRect(packet, {myRect.X, myRect.Y, myRect.W, t}, bc, whiteTex, whiteSampler, FillLayer(*this));
        PushRect(packet, {myRect.X, myRect.Y + myRect.H - t, myRect.W, t}, bc, whiteTex, whiteSampler,
                 FillLayer(*this));
        PushRect(packet, {myRect.X, myRect.Y, t, myRect.H}, bc, whiteTex, whiteSampler, FillLayer(*this));
        PushRect(packet, {myRect.X + myRect.W - t, myRect.Y, t, myRect.H}, bc, whiteTex, whiteSampler,
                 FillLayer(*this));
    }
}

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

void Label::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                      TextureHandle /*whiteTex*/, SamplerHandle /*whiteSampler*/,
                      const UISkin& skin)
{
    if (!skin.Font || Text.empty()) return;

    const Color col = WithAlpha(Enabled ? skin.TextLabel : skin.TextDisabled, effectiveOpacity);

    f32 textX = myRect.X;
    if (Align == TextAlign::Center || AutoCenter) textX = myRect.X + myRect.W * 0.5f;
    else if (Align == TextAlign::Right)           textX = myRect.X + myRect.W;

    const std::string& display = WordWrap ? WrapText(*skin.Font, Text, myRect.W, skin.FontScale)
                                          : Text;
    DrawTextUI(packet, *skin.Font, display, { textX, myRect.Y }, skin.FontScale, col,
               TextLayer(*this), Align);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

void Button::OnUpdate(f32 /*dt*/, const UIRect myRect, const Vec2                            mousePos,
                      const bool               mouseDown, bool /*mouseJustDown*/, const bool mouseJustUp,
                      bool&                    consumed)
{
    if (consumed) {
        _hovered = _pressed = false;
        return;
    }

    _hovered = myRect.Contains(mousePos);
    if (!_hovered) {
        _pressed = false;
        return;
    }

    if (mouseDown) _pressed = true;

    if (mouseJustUp && _pressed) {
        _pressed = false;
        consumed = true;
        if (OnClick) OnClick();
    }
}

void Button::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                       const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                       const UISkin&       skin)
{
    Color bg;
    if (!Enabled)              bg = skin.ButtonDisabled;
    else if (_pressed)         bg = skin.ButtonPressed;
    else if (_hovered || _focused) bg = skin.ButtonHovered;
    else                       bg = skin.ButtonNormal;

    PushRect(packet, myRect, WithAlpha(bg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this));

    if (skin.Font && !Text.empty()) {
        const Color col = WithAlpha(Enabled ? skin.TextNormal : skin.TextDisabled, effectiveOpacity);
        const Vec2  pos = {
            myRect.X + myRect.W * 0.5f, myRect.Y + myRect.H * 0.5f
            - skin.Font->GetAscent() * skin.FontScale * 0.5f
        };
        DrawTextUI(packet, *skin.Font, Text, pos, skin.FontScale, col,
                   TextLayer(*this), TextAlign::Center);
    }
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------

void Image::OnCollect(FramePacket& packet, UIRect myRect, const f32 effectiveOpacity,
                      TextureHandle /*whiteTex*/, SamplerHandle /*whiteSampler*/,
                      const UISkin& skin)
{
    if (!Texture.IsValid()) return;

    Sprite s{};
    s.Texture  = Texture;
    s.Sampler  = Sampler;
    s.Position = {myRect.X + myRect.W * 0.5f, myRect.Y + myRect.H * 0.5f};
    s.Size     = {myRect.W, myRect.H};
    s.UV       = UV;
    s.Tint     = WithAlpha(Tint, effectiveOpacity);
    s.Layer    = FillLayer(*this);
    packet.UISprites.push_back(s);
}

// ---------------------------------------------------------------------------
// NineSlice
// ---------------------------------------------------------------------------

void NineSlice::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                           TextureHandle /*whiteTex*/, SamplerHandle /*whiteSampler*/,
                           const UISkin& /*skin*/)
{
    if (!Texture.IsValid()) return;
    const Color tint  = WithAlpha(Tint, effectiveOpacity);
    const i32   layer = FillLayer(*this);

    const f32 pL = std::min(PixelLeft,   myRect.W * 0.5f);
    const f32 pR = std::min(PixelRight,  myRect.W * 0.5f);
    const f32 pT = std::min(PixelTop,    myRect.H * 0.5f);
    const f32 pB = std::min(PixelBottom, myRect.H * 0.5f);
    const f32 cX = myRect.X + pL,           cY = myRect.Y + pT;
    const f32 cW = myRect.W - pL - pR,      cH = myRect.H - pT - pB;
    const f32 rX = myRect.X + myRect.W - pR, bY = myRect.Y + myRect.H - pB;
    const f32 uvL = UVBorderLeft,  uvT = UVBorderTop;
    const f32 uvR = 1.0f - UVBorderRight, uvB = 1.0f - UVBorderBottom;

    struct Slice { UIRect s; UVRect uv; };
    const Slice slices[9] = {
        {{ myRect.X, myRect.Y, pL, pT }, { 0,   0,   uvL, uvT }},
        {{ cX,       myRect.Y, cW, pT }, { uvL, 0,   uvR, uvT }},
        {{ rX,       myRect.Y, pR, pT }, { uvR, 0,   1,   uvT }},
        {{ myRect.X, cY,       pL, cH }, { 0,   uvT, uvL, uvB }},
        {{ cX,       cY,       cW, cH }, { uvL, uvT, uvR, uvB }},
        {{ rX,       cY,       pR, cH }, { uvR, uvT, 1,   uvB }},
        {{ myRect.X, bY,       pL, pB }, { 0,   uvB, uvL, 1   }},
        {{ cX,       bY,       cW, pB }, { uvL, uvB, uvR, 1   }},
        {{ rX,       bY,       pR, pB }, { uvR, uvB, 1,   1   }},
    };
    for (const auto& [sr, uv] : slices)
        PushTexturedRect(packet, sr, uv, tint, Texture, Sampler, layer);
}

// ---------------------------------------------------------------------------
// ProgressBar
// ---------------------------------------------------------------------------

void ProgressBar::OnCollect(FramePacket&        packet, const UIRect          myRect, const f32 effectiveOpacity,
                            const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                            const UISkin&       skin)
{
    PushRect(packet, myRect, WithAlpha(skin.ProgressBg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this));

    const f32    fillW = myRect.W * std::clamp(Value, 0.0f, 1.0f);
    const Color  fillC = (FillColor.A > 0.0f) ? FillColor : skin.ProgressFill;
    const UIRect fillR = {myRect.X, myRect.Y, fillW, myRect.H};
    PushRect(packet, fillR, WithAlpha(fillC, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this));
}
} // namespace Arcbit
