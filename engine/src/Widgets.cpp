#include <arcbit/ui/Widgets.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/render/RenderThread.h>
#include <arcbit/render/Font.h>

#include <algorithm>

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
static Color WithAlpha(Color c, const f32 alpha)
{
    c.A *= alpha;
    return c;
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

    // Determine text anchor X based on alignment.
    f32 textX = myRect.X;
    if (Align == TextAlign::Center || AutoCenter)
        textX = myRect.X + myRect.W * 0.5f;
    else if (Align == TextAlign::Right)
        textX = myRect.X + myRect.W;

    const Vec2 pos = {textX, myRect.Y};
    DrawTextUI(packet, *skin.Font, Text, pos, skin.FontScale, col, TextLayer(*this), Align);
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
    if (!Enabled) bg = skin.ButtonDisabled;
    else if (_pressed) bg = skin.ButtonPressed;
    else if (_hovered) bg = skin.ButtonHovered;
    else bg               = skin.ButtonNormal;

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
