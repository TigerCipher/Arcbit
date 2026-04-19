#include <arcbit/ui/UIWidget.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/render/RenderThread.h>

#include <algorithm>

namespace Arcbit
{
UIRect UIWidget::ComputeRect(const UIRect parent) const
{
    const f32 w = (SizePercent.X > 0.0f) ? parent.W * SizePercent.X : Size.X;
    const f32 h = (SizePercent.Y > 0.0f) ? parent.H * SizePercent.Y : Size.Y;

    const f32 anchorX = parent.X + parent.W * Anchor.X;
    const f32 anchorY = parent.Y + parent.H * Anchor.Y;

    return {
        anchorX - w * Pivot.X + Offset.X,
        anchorY - h * Pivot.Y + Offset.Y,
        w, h
    };
}

void UIWidget::UpdateTree(const f32  dt, const UIRect      parent, const Vec2        mousePos,
                          const bool mouseDown, const bool mouseJustDown, const bool mouseJustUp,
                          bool&      consumed)
{
    if (!Visible) return;

    const UIRect myRect = ComputeRect(parent);

    // Update children in reverse ZOrder so highest ZOrder gets first crack.
    std::vector<UIWidget*> sorted;
    sorted.reserve(_children.size());
    for (auto& c : _children) sorted.push_back(c.get());
    std::sort(sorted.begin(), sorted.end(),
              [](const UIWidget* a, const UIWidget* b) { return a->ZOrder > b->ZOrder; });

    for (UIWidget* child : sorted)
        child->UpdateTree(dt, myRect, mousePos, mouseDown, mouseJustDown, mouseJustUp, consumed);

    if (Enabled)
        OnUpdate(dt, myRect, mousePos, mouseDown, mouseJustDown, mouseJustUp, consumed);
}

void UIWidget::CollectTree(FramePacket&        packet, const UIRect          parent, const f32 parentOpacity,
                           const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                           const UISkin&       skin)
{
    if (!Visible) return;

    const UIRect myRect         = ComputeRect(parent);
    const f32    effectiveAlpha = Opacity * parentOpacity;

    OnCollect(packet, myRect, effectiveAlpha, whiteTex, whiteSampler, skin);

    // Children sorted by ascending ZOrder so higher values render on top.
    std::vector<UIWidget*> sorted;
    sorted.reserve(_children.size());
    for (auto& c : _children) sorted.push_back(c.get());
    std::ranges::sort(sorted,
                      [](const UIWidget* a, const UIWidget* b) { return a->ZOrder < b->ZOrder; });

    for (UIWidget* child : sorted)
        child->CollectTree(packet, myRect, effectiveAlpha, whiteTex, whiteSampler, skin);
}
} // namespace Arcbit
