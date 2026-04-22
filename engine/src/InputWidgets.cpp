#include <arcbit/ui/InputWidgets.h>
#include <arcbit/ui/UISkin.h>
#include <arcbit/render/Font.h>
#include <arcbit/render/RenderThread.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <string>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// Rendering helpers (mirrors Widgets.cpp — not shared to avoid coupling)
// ---------------------------------------------------------------------------

static i32 BgLayer(const UIWidget& w, const UISkin& skin)
{
    return UIBaseLayer + (skin.ScreenLayerBase + w.ZOrder) * UILayerStride + UILayerBg;
}

static i32 FillLayer(const UIWidget& w, const UISkin& skin)
{
    return UIBaseLayer + (skin.ScreenLayerBase + w.ZOrder) * UILayerStride + UILayerFill;
}

static i32 TextLayer(const UIWidget& w, const UISkin& skin)
{
    return UIBaseLayer + (skin.ScreenLayerBase + w.ZOrder) * UILayerStride + UILayerText;
}

static void PushRect(FramePacket& packet, const UIRect& r, const Color& c,
                     const TextureHandle tex, const SamplerHandle samp, const i32 layer)
{
    if (r.W <= 0.0f || r.H <= 0.0f) return;
    Sprite s{};
    s.Texture  = tex;
    s.Sampler  = samp;
    s.Position = { r.X + r.W * 0.5f, r.Y + r.H * 0.5f };
    s.Size     = { r.W, r.H };
    s.Tint     = c;
    s.Layer    = layer;
    packet.UISprites.push_back(s);
}

static Color WithAlpha(Color c, const f32 alpha) { c.A *= alpha; return c; }

// ---------------------------------------------------------------------------
// TextInput
// ---------------------------------------------------------------------------

bool TextInput::AcceptsChar(const char c) const
{
    if (c < 32 || c >= 127) return false; // non-printable ASCII
    switch (InputMode) {
        case Mode::Text:    return true;
        case Mode::Numeric: return std::isdigit(c) || c == '.' || c == '-';
        case Mode::Regex:
            if (Pattern.empty()) return true;
            try {
                const std::regex re(Pattern);
                const char buf[2] = { c, '\0' };
                return std::regex_match(buf, re);
            } catch (...) { return false; }
    }
    return false;
}

void TextInput::ClampCursor()
{
    _cursor = static_cast<u32>(std::min(static_cast<size_t>(_cursor), Text.size()));
}

void TextInput::OnTextInput(const std::string_view chars)
{
    if (!_focused) return;
    for (const char c : chars) {
        if (!AcceptsChar(c)) continue;
        if (Text.size() >= MaxLength) continue;
        Text.insert(Text.begin() + _cursor, c);
        ++_cursor;
    }
    if (OnChanged) OnChanged(Text);
}

void TextInput::OnControlKey(const UIControlKey key)
{
    switch (key) {
        case UIControlKey::Left:
            if (_cursor > 0) --_cursor;
            break;
        case UIControlKey::Right:
            if (_cursor < Text.size()) ++_cursor;
            break;
        case UIControlKey::Home:
            _cursor = 0;
            break;
        case UIControlKey::End:
            _cursor = static_cast<u32>(Text.size());
            break;
        case UIControlKey::Backspace:
            if (_cursor > 0 && !Text.empty()) {
                Text.erase(Text.begin() + --_cursor);
                if (OnChanged) OnChanged(Text);
            }
            break;
        case UIControlKey::Delete:
            if (_cursor < Text.size()) {
                Text.erase(Text.begin() + _cursor);
                if (OnChanged) OnChanged(Text);
            }
            break;
        case UIControlKey::Enter:
            if (OnConfirm) OnConfirm(Text);
            break;
        default:
            break;
    }
}

void TextInput::OnUpdate(const f32 dt, const UIRect myRect, const Vec2 mousePos,
                         const bool /*mouseDown*/, const bool mouseJustDown,
                         const bool /*mouseJustUp*/, bool& consumed)
{
    if (!Enabled) return;
    if (consumed) return;
    const bool over = mousePos.X >= myRect.X && mousePos.X <= myRect.X + myRect.W &&
                      mousePos.Y >= myRect.Y && mousePos.Y <= myRect.Y + myRect.H;
    if (over && mouseJustDown) consumed = true; // focus is handled by UIScreen

    _cursorBlink += dt;
    if (_cursorBlink >= 1.0f) _cursorBlink -= 1.0f;
}

void TextInput::UpdateScroll(const f32 availW, const UISkin& skin)
{
    if (!skin.Font) return;
    const f32 scale    = skin.FontScale;
    const f32 cursorX  = MeasureTextWidth(*skin.Font, Text.substr(0, _cursor), scale);
    const f32 textW    = MeasureTextWidth(*skin.Font, Text, scale);

    // Scroll so the cursor is always visible.
    if (cursorX - _scrollX > availW) _scrollX = cursorX - availW;
    if (cursorX - _scrollX < 0.0f)   _scrollX = cursorX;
    _scrollX = std::clamp(_scrollX, 0.0f, std::max(0.0f, textW - availW));
}

void TextInput::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                          const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                          const UISkin& skin)
{
    const Color borderColor = _focused ? skin.InputFocusBorder : skin.InputBorder;
    PushRect(packet, myRect, WithAlpha(skin.InputBg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin));
    // Border (1-pixel inner outline via slightly-inset rect is common; draw as bg overlay)
    const UIRect border = { myRect.X - 1, myRect.Y - 1, myRect.W + 2, myRect.H + 2 };
    PushRect(packet, border, WithAlpha(borderColor, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin) - 1);

    if (!skin.Font) return;

    const f32 pad    = 4.0f;
    const f32 availW = myRect.W - pad * 2.0f;
    UpdateScroll(availW, skin);

    // Collect text (and cursor) first, then register the clip rect and patch ClipIndex
    // on all quads emitted — same pattern as ScrollPanel::CollectTree.
    const size_t firstSprite = packet.UISprites.size();

    const std::string& display = (Text.empty() && !_focused) ? Placeholder : Text;
    const Color textColor = (Text.empty() && !_focused)
        ? WithAlpha(skin.InputPlaceholder, effectiveOpacity)
        : WithAlpha(skin.TextNormal, effectiveOpacity);

    const f32  textY   = myRect.Y + myRect.H * 0.5f - skin.Font->GetAscent() * skin.FontScale * 0.5f;
    const Vec2 textPos = { myRect.X + pad - _scrollX, textY };
    DrawTextUI(packet, *skin.Font, display, textPos, skin.FontScale, textColor, TextLayer(*this, skin));

    // Cursor blink — visible for first half of 1-second cycle
    if (_focused && _cursorBlink < 0.5f) {
        const f32    cursorX = MeasureTextWidth(*skin.Font, Text.substr(0, _cursor), skin.FontScale);
        const UIRect cursorR = { myRect.X + pad + cursorX - _scrollX, myRect.Y + 3.0f, 1.5f, myRect.H - 6.0f };
        PushRect(packet, cursorR, WithAlpha(skin.InputCursor, effectiveOpacity),
                 whiteTex, whiteSampler, FillLayer(*this, skin));
    }

    // 1px vertical slack so descenders aren't shaved by the scissor rect.
    packet.UIClipRects.push_back({ myRect.X + pad, myRect.Y - 1.0f, availW, myRect.H + 2.0f });
    const u16 clipIdx = static_cast<u16>(packet.UIClipRects.size()); // 1-based
    for (size_t i = firstSprite; i < packet.UISprites.size(); ++i)
        if (packet.UISprites[i].ClipIndex == 0)
            packet.UISprites[i].ClipIndex = clipIdx;
}

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------

f32 Slider::NormalizedValue() const
{
    if (Max <= Min) return 0.0f;
    return (Value - Min) / (Max - Min);
}

UIRect Slider::ThumbRect(const UIRect track) const
{
    const f32 t    = NormalizedValue();
    const f32 cx   = track.X + t * track.W;
    const f32 half = _thumbHalf;
    return { cx - half, track.Y - 2.0f, half * 2.0f, track.H + 4.0f };
}

void Slider::SetFromMouseX(const f32 mouseX, const UIRect track)
{
    f32 t = (mouseX - track.X) / track.W;
    t = std::clamp(t, 0.0f, 1.0f);
    f32 raw = Min + t * (Max - Min);
    if (Step > 0.0f) raw = std::round(raw / Step) * Step;
    Value = std::clamp(raw, Min, Max);
    if (OnChanged) OnChanged(Value);
}

void Slider::OnUpdate(const f32 /*dt*/, const UIRect myRect, const Vec2 mousePos,
                      const bool mouseDown, const bool mouseJustDown,
                      const bool mouseJustUp, bool& consumed)
{
    if (!Enabled || consumed) return;
    const UIRect track = { myRect.X + _thumbHalf, myRect.Y + myRect.H * 0.5f - 3.0f,
                           myRect.W - _thumbHalf * 2.0f, 6.0f };
    const UIRect thumb = ThumbRect(track);
    const bool overThumb = mousePos.X >= thumb.X && mousePos.X <= thumb.X + thumb.W &&
                           mousePos.Y >= myRect.Y && mousePos.Y <= myRect.Y + myRect.H;
    _hovered = overThumb;

    if (mouseJustDown && overThumb) { _dragging = true; consumed = true; }
    if (mouseJustUp)                  _dragging = false;
    if (_dragging && mouseDown) {
        SetFromMouseX(mousePos.X, track);
        consumed = true;
    }
}

void Slider::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                       const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                       const UISkin& skin)
{
    const UIRect track = { myRect.X + _thumbHalf, myRect.Y + myRect.H * 0.5f - 3.0f,
                           myRect.W - _thumbHalf * 2.0f, 6.0f };
    PushRect(packet, track, WithAlpha(skin.ProgressBg, effectiveOpacity),
             whiteTex, whiteSampler, BgLayer(*this, skin));

    // Fill from left edge to thumb
    const UIRect fill = { track.X, track.Y, NormalizedValue() * track.W, track.H };
    PushRect(packet, fill, WithAlpha(skin.AccentColor, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));

    const UIRect thumb = ThumbRect(track);
    const Color thumbColor = (_hovered || _dragging || _focused)
        ? skin.SliderThumbHovered : skin.SliderThumb;
    PushRect(packet, thumb, WithAlpha(thumbColor, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));
}

// ---------------------------------------------------------------------------
// Dropdown
// ---------------------------------------------------------------------------

void Dropdown::UpdateTree(const f32 dt, const UIRect parent, const Vec2 mousePos,
                          const bool mouseDown, const bool mouseJustDown,
                          const bool mouseJustUp, bool& consumed, const f32 scrollDelta)
{
    const UIRect myRect = ComputeRect(parent);
    if (!Visible || !Enabled) return;

    // When open, consume all input to prevent pass-through to widgets behind.
    if (_open) consumed = true;

    const bool overHeader = mousePos.X >= myRect.X && mousePos.X <= myRect.X + myRect.W &&
                            mousePos.Y >= myRect.Y && mousePos.Y <= myRect.Y + myRect.H;
    _headerHovered = overHeader;

    if (_open) {
        // Build list rect below the header
        const UIRect listRect = { myRect.X, myRect.Y + myRect.H,
                                  myRect.W, _itemHeight * static_cast<f32>(Items.size()) };
        const bool overList = mousePos.X >= listRect.X && mousePos.X <= listRect.X + listRect.W &&
                              mousePos.Y >= listRect.Y && mousePos.Y <= listRect.Y + listRect.H;

        _hoveredIndex = -1;
        if (overList) {
            const i32 idx = static_cast<i32>((mousePos.Y - listRect.Y) / _itemHeight);
            if (idx >= 0 && idx < static_cast<i32>(Items.size())) _hoveredIndex = idx;
        }

        if (mouseJustDown) {
            if (overList && _hoveredIndex >= 0) {
                SelectedIndex = _hoveredIndex;
                if (OnChanged) OnChanged(SelectedIndex);
            }
            _open = false; // close on any click
        }
    } else {
        if (mouseJustDown && overHeader) _open = true;
    }
}

void Dropdown::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                         const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                         const UISkin& skin)
{
    const Color bg = _headerHovered ? skin.ButtonHovered : skin.ButtonNormal;
    PushRect(packet, myRect, WithAlpha(bg, effectiveOpacity), whiteTex, whiteSampler, BgLayer(*this, skin));

    if (!skin.Font) return;
    const std::string display = (SelectedIndex >= 0 && SelectedIndex < static_cast<i32>(Items.size()))
        ? Items[static_cast<size_t>(SelectedIndex)] : "—";
    DrawTextUI(packet, *skin.Font, display,
               { myRect.X + 6.0f, myRect.Y + myRect.H * 0.5f - skin.FontScale * 8.0f },
               skin.FontScale, WithAlpha(skin.TextNormal, effectiveOpacity), TextLayer(*this, skin));

    // Arrow indicator on the right
    const std::string_view arrow = _open ? "▲" : "▼";
    DrawTextUI(packet, *skin.Font, arrow,
               { myRect.X + myRect.W - 20.0f, myRect.Y + myRect.H * 0.5f - skin.FontScale * 8.0f },
               skin.FontScale, WithAlpha(skin.TextDisabled, effectiveOpacity), TextLayer(*this, skin));
}

void Dropdown::CollectList(FramePacket& packet, const UIRect headerRect, const f32 opacity,
                           const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                           const UISkin& skin) const
{
    // Overlay ZOrder for the expanded list — must sort above sibling widgets.
    const i32 listZOrder = ZOrder + 500;
    const i32 bgL  = UIBaseLayer + (skin.ScreenLayerBase + listZOrder) * UILayerStride + UILayerBg;
    const i32 txtL = UIBaseLayer + (skin.ScreenLayerBase + listZOrder) * UILayerStride + UILayerText;

    for (i32 i = 0; i < static_cast<i32>(Items.size()); ++i) {
        const UIRect row = { headerRect.X, headerRect.Y + headerRect.H + _itemHeight * static_cast<f32>(i),
                             headerRect.W, _itemHeight };
        const Color bg  = (i == _hoveredIndex) ? skin.ButtonHovered
                        : (i == SelectedIndex)  ? skin.ButtonPressed
                        :                         skin.PanelBg;
        PushRect(packet, row, WithAlpha(bg, opacity), whiteTex, whiteSampler, bgL);
        if (skin.Font)
            DrawTextUI(packet, *skin.Font, Items[static_cast<size_t>(i)],
                       { row.X + 6.0f, row.Y + row.H * 0.5f - skin.FontScale * 8.0f },
                       skin.FontScale, WithAlpha(skin.TextNormal, opacity), txtL);
    }
}

void Dropdown::CollectTree(FramePacket& packet, const UIRect parent, const f32 parentOpacity,
                           const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                           const UISkin& skin)
{
    if (!Visible) return;
    const UIRect myRect = ComputeRect(parent);
    const f32    eff    = parentOpacity * Opacity;
    OnCollect(packet, myRect, eff, whiteTex, whiteSampler, skin);
    if (_open) CollectList(packet, myRect, eff, whiteTex, whiteSampler, skin);
    for (auto& child : _children)
        child->CollectTree(packet, myRect, eff, whiteTex, whiteSampler, skin);
}

// ---------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------

void Checkbox::OnActivate()
{
    Checked = !Checked;
    if (OnChanged) OnChanged(Checked);
}

void Checkbox::OnUpdate(const f32 /*dt*/, const UIRect myRect, const Vec2 mousePos,
                        const bool /*mouseDown*/, const bool mouseJustDown,
                        const bool /*mouseJustUp*/, bool& consumed)
{
    if (!Enabled || consumed) return;
    const bool over = mousePos.X >= myRect.X && mousePos.X <= myRect.X + myRect.W &&
                      mousePos.Y >= myRect.Y && mousePos.Y <= myRect.Y + myRect.H;
    _hovered = over;
    if (over && mouseJustDown) { OnActivate(); consumed = true; }
}

void Checkbox::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                         const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                         const UISkin& skin)
{
    const f32    boxSize = std::min(myRect.H, 18.0f);
    const UIRect box     = { myRect.X, myRect.Y + (myRect.H - boxSize) * 0.5f, boxSize, boxSize };
    const Color  bg      = _hovered ? skin.CheckboxHovered : skin.CheckboxBg;
    PushRect(packet, box, WithAlpha(bg, effectiveOpacity), whiteTex, whiteSampler, BgLayer(*this, skin));

    if (Checked) {
        const UIRect inner = { box.X + 3.0f, box.Y + 3.0f, box.W - 6.0f, box.H - 6.0f };
        PushRect(packet, inner, WithAlpha(skin.CheckboxCheck, effectiveOpacity),
                 whiteTex, whiteSampler, FillLayer(*this, skin));
    }

    if (!Label.empty() && skin.Font) {
        DrawTextUI(packet, *skin.Font, Label,
                   { box.X + boxSize + 6.0f, myRect.Y + myRect.H * 0.5f - skin.FontScale * 8.0f },
                   skin.FontScale, WithAlpha(skin.TextNormal, effectiveOpacity), TextLayer(*this, skin));
    }
}

// ---------------------------------------------------------------------------
// RadioGroup
// ---------------------------------------------------------------------------

void RadioGroup::OnUpdate(const f32 /*dt*/, const UIRect myRect, const Vec2 mousePos,
                          const bool /*mouseDown*/, const bool mouseJustDown,
                          const bool /*mouseJustUp*/, bool& consumed)
{
    if (!Enabled || consumed) return;
    _hoveredIndex = -1;
    for (i32 i = 0; i < static_cast<i32>(Items.size()); ++i) {
        const UIRect row = { myRect.X, myRect.Y + ItemHeight * static_cast<f32>(i), myRect.W, ItemHeight };
        const bool over  = mousePos.X >= row.X && mousePos.X <= row.X + row.W &&
                           mousePos.Y >= row.Y && mousePos.Y <= row.Y + row.H;
        if (over) _hoveredIndex = i;
        if (over && mouseJustDown) {
            SelectedIndex = i;
            if (OnChanged) OnChanged(SelectedIndex);
            consumed = true;
        }
    }
}

void RadioGroup::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                           const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                           const UISkin& skin)
{
    const f32 dotR = 7.0f; // outer circle radius

    for (i32 i = 0; i < static_cast<i32>(Items.size()); ++i) {
        const UIRect row = { myRect.X, myRect.Y + ItemHeight * static_cast<f32>(i), myRect.W, ItemHeight };
        const f32    cy  = row.Y + ItemHeight * 0.5f;
        const UIRect dot = { row.X, cy - dotR, dotR * 2.0f, dotR * 2.0f };
        const Color  bg  = (i == _hoveredIndex) ? skin.CheckboxHovered : skin.CheckboxBg;
        PushRect(packet, dot, WithAlpha(bg, effectiveOpacity), whiteTex, whiteSampler, BgLayer(*this, skin));

        if (i == SelectedIndex) {
            const f32    innerR = dotR - 3.0f;
            const UIRect inner  = { dot.X + dotR - innerR, dot.Y + dotR - innerR, innerR * 2.0f, innerR * 2.0f };
            PushRect(packet, inner, WithAlpha(skin.CheckboxCheck, effectiveOpacity),
                     whiteTex, whiteSampler, FillLayer(*this, skin));
        }

        if (skin.Font)
            DrawTextUI(packet, *skin.Font, Items[static_cast<size_t>(i)],
                       { row.X + dotR * 2.0f + 6.0f, cy - skin.FontScale * 8.0f },
                       skin.FontScale, WithAlpha(skin.TextNormal, effectiveOpacity), TextLayer(*this, skin));
    }
}

// ---------------------------------------------------------------------------
// Switch
// ---------------------------------------------------------------------------

void Switch::OnActivate()
{
    On = !On;
    if (OnChanged) OnChanged(On);
}

void Switch::OnUpdate(const f32 dt, const UIRect myRect, const Vec2 mousePos,
                      const bool /*mouseDown*/, const bool mouseJustDown,
                      const bool /*mouseJustUp*/, bool& consumed)
{
    if (!Enabled) return;
    const bool over = mousePos.X >= myRect.X && mousePos.X <= myRect.X + myRect.W &&
                      mousePos.Y >= myRect.Y && mousePos.Y <= myRect.Y + myRect.H;
    _hovered = over;
    if (over && mouseJustDown && !consumed) { OnActivate(); consumed = true; }

    // Animate thumb toward target
    const f32 target = On ? 1.0f : 0.0f;
    const f32 delta  = target - _animT;
    _animT += std::copysign(std::min(std::abs(delta), AnimSpeed * dt), delta);
}

void Switch::OnCollect(FramePacket& packet, const UIRect myRect, const f32 effectiveOpacity,
                       const TextureHandle whiteTex, const SamplerHandle whiteSampler,
                       const UISkin& skin)
{
    // Lerp track color between off and on
    const Color trackOff = skin.SwitchOff;
    const Color trackOn  = skin.SwitchOn;
    Color track;
    track.R = trackOff.R + (trackOn.R - trackOff.R) * _animT;
    track.G = trackOff.G + (trackOn.G - trackOff.G) * _animT;
    track.B = trackOff.B + (trackOn.B - trackOff.B) * _animT;
    track.A = 1.0f;
    PushRect(packet, myRect, WithAlpha(track, effectiveOpacity), whiteTex, whiteSampler, BgLayer(*this, skin));

    const f32    pad    = 2.0f;
    const f32    thumbW = myRect.H - pad * 2.0f;
    const f32    travel = myRect.W - thumbW - pad * 2.0f;
    const UIRect thumb  = { myRect.X + pad + _animT * travel, myRect.Y + pad, thumbW, thumbW };
    PushRect(packet, thumb, WithAlpha(skin.SwitchThumb, effectiveOpacity),
             whiteTex, whiteSampler, FillLayer(*this, skin));
}

} // namespace Arcbit
