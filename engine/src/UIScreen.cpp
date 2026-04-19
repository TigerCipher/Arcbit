#include <arcbit/ui/UIScreen.h>
#include <arcbit/render/RenderThread.h>

#include <algorithm>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Widget tree update / collect
// ---------------------------------------------------------------------------

void UIScreen::Update(const f32 dt, const UIRect screenRect, const Vec2 mousePos,
                      const bool mouseDown, const bool mouseJustDown, const bool mouseJustUp)
{
    if (!_visible) return;

    std::vector<UIWidget*> sorted;
    sorted.reserve(_roots.size());
    for (auto& w : _roots) sorted.push_back(w.get());
    std::ranges::sort(sorted,
                      [](const UIWidget* a, const UIWidget* b) { return a->ZOrder > b->ZOrder; });

    bool consumed = false;
    for (UIWidget* w : sorted)
        w->UpdateTree(dt, screenRect, mousePos, mouseDown, mouseJustDown, mouseJustUp, consumed);
}

void UIScreen::Collect(FramePacket& packet, const UIRect screenRect, const UISkin& skin,
                       const TextureHandle whiteTex, const SamplerHandle whiteSampler)
{
    if (!_visible) return;

    std::vector<UIWidget*> sorted;
    sorted.reserve(_roots.size());
    for (auto& w : _roots) sorted.push_back(w.get());
    std::ranges::sort(sorted,
                      [](const UIWidget* a, const UIWidget* b) { return a->ZOrder < b->ZOrder; });

    for (UIWidget* w : sorted)
        w->CollectTree(packet, screenRect, _transitionOpacity, whiteTex, whiteSampler, skin);
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

void UIScreen::AdvanceTransition(const f32 dt)
{
    if (_transitionState == TransitionState::FadingIn) {
        _transitionOpacity = std::min(1.0f, _transitionOpacity + TransitionSpeed * dt);
        if (_transitionOpacity >= 1.0f) _transitionState = TransitionState::Idle;
    } else if (_transitionState == TransitionState::FadingOut) {
        _transitionOpacity = std::max(0.0f, _transitionOpacity - TransitionSpeed * dt);
    }
}

// ---------------------------------------------------------------------------
// Focus navigation
// ---------------------------------------------------------------------------

void UIScreen::CollectFocusables(UIWidget& w, std::vector<UIWidget*>& out)
{
    if (w.Visible && w.Enabled && w.Focusable)
        out.push_back(&w);
    for (auto& child : w._children)
        CollectFocusables(*child, out);
}

std::vector<UIWidget*> UIScreen::GatherFocusables() const
{
    std::vector<UIWidget*> result;
    for (auto& root : _roots)
        CollectFocusables(*root, result);
    return result;
}

void UIScreen::ApplyFocus(UIWidget* widget)
{
    if (_focusedWidget == widget) return;
    if (_focusedWidget) { _focusedWidget->_focused = false; _focusedWidget->OnFocusLost(); }
    _focusedWidget = widget;
    if (_focusedWidget) { _focusedWidget->_focused = true;  _focusedWidget->OnFocusGained(); }
}

void UIScreen::FocusNext()
{
    const auto fs = GatherFocusables();
    if (fs.empty()) return;
    i32 cur = -1;
    for (i32 i = 0; i < static_cast<i32>(fs.size()); ++i)
        if (fs[i] == _focusedWidget) { cur = i; break; }
    ApplyFocus(fs[static_cast<u32>(cur + 1) % fs.size()]);
}

void UIScreen::FocusPrev()
{
    const auto fs = GatherFocusables();
    if (fs.empty()) return;
    i32 cur = 0;
    for (i32 i = 0; i < static_cast<i32>(fs.size()); ++i)
        if (fs[i] == _focusedWidget) { cur = i; break; }
    const i32 n = static_cast<i32>(fs.size());
    ApplyFocus(fs[static_cast<u32>((cur - 1 + n) % n)]);
}

void UIScreen::ActivateFocused()
{
    if (_focusedWidget) _focusedWidget->OnActivate();
}

void UIScreen::ClearFocus()
{
    ApplyFocus(nullptr);
}

} // namespace Arcbit
