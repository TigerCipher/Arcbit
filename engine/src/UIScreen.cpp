#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/UILoader.h>
#include <arcbit/render/RenderThread.h>

#include <algorithm>

#include "arcbit/core/Log.h"

namespace Arcbit {

// ---------------------------------------------------------------------------
// Layout loading + meta
// ---------------------------------------------------------------------------

UIWidget* UIScreen::AddRaw(std::unique_ptr<UIWidget> w)
{
    UIWidget* ptr = w.get();
    _roots.push_back(std::move(w));
    return ptr;
}

bool UIScreen::LoadLayout(const std::string_view path)
{
    LOG_INFO(UI, "Loading UI layout from: {}", path);
    _roots.clear();
    _metaF32.clear();
    _metaStr.clear();
    return UILoader::Load(path, *this);
}

f32 UIScreen::GetMetaF32(const std::string_view key, const f32 def) const
{
    const auto it = _metaF32.find(std::string(key));
    return it != _metaF32.end() ? it->second : def;
}

std::string UIScreen::GetMetaStr(const std::string_view key, const std::string_view def) const
{
    const auto it = _metaStr.find(std::string(key));
    return it != _metaStr.end() ? it->second : std::string(def);
}

void UIScreen::SetMetaF32(const std::string_view key, const f32 value)
{
    _metaF32[std::string(key)] = value;
}

void UIScreen::SetMetaStr(const std::string_view key, const std::string_view value)
{
    _metaStr[std::string(key)] = std::string(value);
}

// ---------------------------------------------------------------------------
// Widget tree update / collect
// ---------------------------------------------------------------------------

UIWidget* UIScreen::FindFocusableHit(UIWidget& w, const UIRect parent, const Vec2 pos) const
{
    if (!w.Visible || !w.Enabled) return nullptr;
    const UIRect myRect = w.ComputeRect(parent);
    // Children checked first in reverse ZOrder (highest = topmost visually)
    for (auto it = w._children.rbegin(); it != w._children.rend(); ++it)
        if (auto* hit = FindFocusableHit(**it, myRect, pos)) return hit;
    if (w.Focusable && myRect.Contains(pos)) return &w;
    return nullptr;
}

void UIScreen::Update(const f32 dt, const UIRect screenRect, const Vec2 mousePos,
                      const bool mouseDown, const bool mouseJustDown, const bool mouseJustUp,
                      const f32 scrollDelta)
{
    if (!_visible) return;

    std::vector<UIWidget*> sorted;
    sorted.reserve(_roots.size());
    for (auto& w : _roots) sorted.push_back(w.get());
    std::ranges::sort(sorted,
                      [](const UIWidget* a, const UIWidget* b) { return a->ZOrder > b->ZOrder; });

    bool consumed = false;
    for (UIWidget* w : sorted)
        w->UpdateTree(dt, screenRect, mousePos, mouseDown, mouseJustDown, mouseJustUp, consumed, scrollDelta);

    // Click-to-focus: when the mouse button goes down, find the topmost Focusable
    // widget under the cursor and give it focus. This lets TextInput (and other
    // keyboard-driven widgets) receive focus without requiring Tab navigation.
    if (mouseJustDown) {
        for (UIWidget* w : sorted)
            if (auto* hit = FindFocusableHit(*w, screenRect, mousePos)) {
                ApplyFocus(hit);
                break;
            }
    }
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
// Text input dispatch
// ---------------------------------------------------------------------------

void UIScreen::DispatchTextInput(const std::string_view chars)
{
    if (_focusedWidget) _focusedWidget->OnTextInput(chars);
}

void UIScreen::DispatchControlKey(const UIControlKey key)
{
    if (_focusedWidget) _focusedWidget->OnControlKey(key);
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
