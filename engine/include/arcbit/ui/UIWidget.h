#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/core/Math.h>
#include <arcbit/render/RenderHandle.h>

#include <functional>
#include <memory>
#include <vector>

namespace Arcbit
{
struct FramePacket;
struct UISkin;

// ---------------------------------------------------------------------------
// UIRect — axis-aligned screen-space rect in pixels (top-left origin).
// ---------------------------------------------------------------------------
struct UIRect
{
    f32 X = 0, Y = 0, W = 0, H = 0;

    [[nodiscard]] bool Contains(const Vec2 p) const { return p.X >= X && p.X < X + W && p.Y >= Y && p.Y < Y + H; }
};

// Base layer offset for all UI quads.  Each widget ZOrder level uses 4
// sublayers: 0=background, 1=fill/foreground, 2=text, 3=reserved.
static constexpr i32 UIBaseLayer   = 10'000'000;
static constexpr i32 UILayerStride = 4;
static constexpr i32 UILayerBg     = 0;
static constexpr i32 UILayerFill   = 1;
static constexpr i32 UILayerText   = 2;

// ---------------------------------------------------------------------------
// UIWidget — base class for all retained-mode UI widgets.
//
// Position is expressed as an anchor point on the parent rect plus a pixel
// offset, allowing layouts to adapt to different window sizes automatically.
//
// Children are owned by the parent and rendered in ascending ZOrder.
// ---------------------------------------------------------------------------
class UIWidget
{
public:
    // --- Layout fields -------------------------------------------------------

    // Normalized (0–1) point on the parent rect that this widget aligns to.
    // {0,0} = top-left, {0.5,0.5} = center, {1,1} = bottom-right.
    Vec2 Anchor = {0.0f, 0.0f};

    // Normalized point on this widget that aligns to the anchor.
    // {0,0} = top-left corner of the widget aligns to the anchor (default).
    // {0.5,0.5} = center of the widget aligns to the anchor.
    Vec2 Pivot = {0.0f, 0.0f};

    // Pixel offset applied after anchor/pivot alignment.
    Vec2 Offset = {0.0f, 0.0f};

    // Widget size in pixels.  Ignored per-axis when SizePercent > 0.
    Vec2 Size = {100.0f, 100.0f};

    // Overrides Size per axis as a fraction of the parent's size when > 0.
    // e.g. {1.0, 0} = full parent width, fixed height from Size.Y.
    Vec2 SizePercent = {0.0f, 0.0f};

    // --- Visual / interaction fields ----------------------------------------

    // Opacity multiplier applied to this widget and all its children.
    f32 Opacity = 1.0f;

    bool Visible = true; // false = not drawn and not hit-tested
    bool Enabled = true; // false = drawn at reduced opacity but not interactive

    // Sort key within the same screen layer.  Higher values render on top.
    i32 ZOrder = 0;

    virtual ~UIWidget() = default;

    // Compute this widget's pixel rect relative to parentRect.
    [[nodiscard]] UIRect ComputeRect(UIRect parent) const;

    // Add a child widget. Returns a raw pointer for convenience; ownership
    // stays with this widget.
    template <typename T, typename... Args>
    T* AddChild(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T*   ptr   = child.get();
        _children.push_back(std::move(child));
        return ptr;
    }

protected:
    // Called once per frame. Override to update interactive state.
    // myRect: this widget's computed pixel rect this frame.
    // mousePos: cursor position in screen pixels.
    // mouseDown: left button currently held.
    // mouseJustDown/Up: button transitioned this frame.
    // consumed: set true if this widget handled the mouse event (prevents
    //           lower-ZOrder siblings from also receiving it).
    virtual void OnUpdate(f32   dt, UIRect      myRect, Vec2        mousePos,
                          bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                          bool& consumed) {}

    // Emit render quads into the packet for this widget only (not children).
    // whiteTex / whiteSampler: 1×1 white texture for solid-color quads.
    virtual void OnCollect(FramePacket&  packet, UIRect          myRect, f32 effectiveOpacity,
                           TextureHandle whiteTex, SamplerHandle whiteSampler,
                           const UISkin& skin) = 0;

    std::vector<std::unique_ptr<UIWidget>> _children;

private:
    friend class UIScreen;

    void UpdateTree(f32   dt, UIRect      parent, Vec2        mousePos,
                    bool  mouseDown, bool mouseJustDown, bool mouseJustUp,
                    bool& consumed);

    void CollectTree(FramePacket&  packet, UIRect          parent, f32 parentOpacity,
                     TextureHandle whiteTex, SamplerHandle whiteSampler,
                     const UISkin& skin);
};
} // namespace Arcbit
