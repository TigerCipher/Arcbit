#include <arcbit/ui/UIScreen.h>
#include <arcbit/render/RenderThread.h>

#include <algorithm>

namespace Arcbit
{
void UIScreen::Update(const f32  dt, const UIRect      screenRect, const Vec2    mousePos,
                      const bool mouseDown, const bool mouseJustDown, const bool mouseJustUp)
{
    if (!_visible) return;

    // Sort roots descending by ZOrder so the topmost widget gets input first.
    std::vector<UIWidget*> sorted;
    sorted.reserve(_roots.size());
    for (auto& w : _roots) sorted.push_back(w.get());
    std::ranges::sort(sorted,
                      [](const UIWidget* a, const UIWidget* b) { return a->ZOrder > b->ZOrder; });

    bool consumed = false;
    for (UIWidget* w : sorted)
        w->UpdateTree(dt, screenRect, mousePos, mouseDown, mouseJustDown, mouseJustUp, consumed);
}

void UIScreen::Collect(FramePacket&        packet, const UIRect          screenRect, const UISkin& skin,
                       const TextureHandle whiteTex, const SamplerHandle whiteSampler)
{
    if (!_visible) return;

    // Collect ascending ZOrder so higher values draw on top.
    std::vector<UIWidget*> sorted;
    sorted.reserve(_roots.size());
    for (auto& w : _roots) sorted.push_back(w.get());
    std::ranges::sort(sorted,
                      [](const UIWidget* a, const UIWidget* b) { return a->ZOrder < b->ZOrder; });

    for (UIWidget* w : sorted)
        w->CollectTree(packet, screenRect, 1.0f, whiteTex, whiteSampler, skin);
}
} // namespace Arcbit
