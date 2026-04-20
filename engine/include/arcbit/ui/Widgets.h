#pragma once

#include <arcbit/ui/UIWidget.h>
#include <arcbit/render/Font.h>
#include <arcbit/core/Math.h>

#include <functional>
#include <string>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Panel — solid background rect; can hold any child widgets.
// ---------------------------------------------------------------------------
class Panel : public UIWidget
{
public:
    bool DrawBorder = false;

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// Label — single- or multi-line text.
// ---------------------------------------------------------------------------
class Label : public UIWidget
{
public:
    std::string Text;
    TextAlign   Align      = TextAlign::Left;
    bool        WordWrap   = false; // wrap lines at word boundaries to fit myRect.W
    bool        AutoCenter = false;
    Color       TextColor  = {0, 0, 0, 0}; // {0,0,0,0} = use skin default

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// Button — clickable rect with label; fires OnClick on mouse-up inside.
// ---------------------------------------------------------------------------
class Button : public UIWidget
{
public:
    std::string           Text;
    std::function<void()> OnClick;
    Color                 TextColor = {0, 0, 0, 0}; // {0,0,0,0} = use skin default

protected:
    void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                  bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

    // Fires OnClick when activated by keyboard/gamepad confirm input.
    void OnActivate() override { if (OnClick) OnClick(); }

private:
    bool _hovered = false;
    bool _pressed = false;
};

// ---------------------------------------------------------------------------
// Image — renders an arbitrary texture scaled to the widget rect.
// ---------------------------------------------------------------------------
class Image : public UIWidget
{
public:
    TextureHandle Texture;
    SamplerHandle Sampler;
    Color         Tint = {1, 1, 1, 1};
    UVRect        UV   = {0, 0, 1, 1};

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// NineSlice — a panel drawn from a 9-patch texture.
//
// The texture is divided into a 3×3 grid by UV-space border fractions.
// Corners are emitted at PixelBorder* sizes; the center stretches to fill.
// ---------------------------------------------------------------------------
class NineSlice : public UIWidget
{
public:
    TextureHandle Texture;
    SamplerHandle Sampler;
    Color         Tint = {1, 1, 1, 1};

    // Source UV-space borders (fraction of texture, 0–1).
    f32 UVBorderLeft   = 0.25f;
    f32 UVBorderRight  = 0.25f;
    f32 UVBorderTop    = 0.25f;
    f32 UVBorderBottom = 0.25f;

    // Rendered border sizes in screen pixels.
    f32 PixelLeft   = 16.0f;
    f32 PixelRight  = 16.0f;
    f32 PixelTop    = 16.0f;
    f32 PixelBottom = 16.0f;

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// ProgressBar — horizontal fill bar (value in [0, 1]).
// ---------------------------------------------------------------------------
class ProgressBar : public UIWidget
{
public:
    f32   Value     = 0.5f;         // 0 = empty, 1 = full
    Color FillColor = {0, 0, 0, 0}; // {0,0,0,0} = use skin default

protected:
    void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};
} // namespace Arcbit
