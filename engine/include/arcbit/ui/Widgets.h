#pragma once

#include <arcbit/ui/UIWidget.h>
#include <arcbit/render/Font.h>
#include <arcbit/core/Math.h>

#include <functional>
#include <string>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Panel — solid background rect; can hold any child widgets.
// ---------------------------------------------------------------------------
class Panel : public UIWidget
{
public:
    bool DrawBorder = false;

protected:
    void OnCollect(FramePacket& packet, UIRect myRect, f32 effectiveOpacity,
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
    TextAlign   Align = TextAlign::Left;

    // If true, position.X is the horizontal center of the widget rect.
    // Combines with the Anchor/Pivot layout system.
    bool AutoCenter = false;

protected:
    void OnCollect(FramePacket& packet, UIRect myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// Button — clickable rect with label; fires OnClick on mouse-up inside.
// ---------------------------------------------------------------------------
class Button : public UIWidget
{
public:
    std::string            Text;
    std::function<void()>  OnClick;

protected:
    void OnUpdate(f32 dt, UIRect myRect, Vec2 mousePos,
                  bool mouseDown, bool mouseJustDown, bool mouseJustUp,
                  bool& consumed) override;

    void OnCollect(FramePacket& packet, UIRect myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;

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
    void OnCollect(FramePacket& packet, UIRect myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

// ---------------------------------------------------------------------------
// ProgressBar — horizontal fill bar (value in [0, 1]).
// ---------------------------------------------------------------------------
class ProgressBar : public UIWidget
{
public:
    f32   Value      = 0.5f;  // 0 = empty, 1 = full
    Color FillColor  = {0, 0, 0, 0};  // {0,0,0,0} = use skin default

protected:
    void OnCollect(FramePacket& packet, UIRect myRect, f32 effectiveOpacity,
                   TextureHandle whiteTex, SamplerHandle whiteSampler,
                   const UISkin& skin) override;
};

} // namespace Arcbit
