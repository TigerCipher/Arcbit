#include "WidgetTestScreen.h"

#include <arcbit/input/InputManager.h>
#include <arcbit/ui/UIManager.h>

#include <format>

namespace Arcbit {

// Escape key action registered by UIManager — we reuse it here.
static constexpr ActionID ActionEsc = MakeAction("UI_TextEscape");

void WidgetTestScreen::OnEnter()
{
    LoadLayout("assets/ui/widget_test.arcui");
    WireWidgets();
    UpdateOutput();
}

void WidgetTestScreen::OnTick(const f32 /*dt*/, const InputManager& input)
{
    // Escape closes the screen only when no widget is focused.
    // (First Escape dismisses TextInput focus; second Escape closes.)
    if (input.JustPressed(ActionEsc) && !GetFocusedWidget())
        if (OnClose) OnClose();
}

void WidgetTestScreen::WireWidgets()
{
    if (auto* btn = FindWidget<Button>("close-btn"))
        btn->OnClick = [this] { if (OnClose) OnClose(); };

    if (auto* ti = FindWidget<TextInput>("text-input"))
        ti->OnChanged = [this](const std::string& v) { _textVal = v; UpdateOutput(); };

    if (auto* ni = FindWidget<TextInput>("num-input"))
        ni->OnChanged = [this](const std::string& v) { _numStr = v; UpdateOutput(); };

    _sliderLabel = FindWidget<Label>("slider-val");
    if (auto* sl = FindWidget<Slider>("volume-slider")) {
        _sliderVal = sl->Value;
        sl->OnChanged = [this](const f32 v) {
            _sliderVal = v;
            if (_sliderLabel) _sliderLabel->Text = std::format("{:.0f}%", v * 100.0f);
            UpdateOutput();
        };
    }

    if (auto* dd = FindWidget<Dropdown>("quality-drop")) {
        _dropIdx = dd->SelectedIndex;
        dd->OnChanged = [this](const i32 idx) { _dropIdx = idx; UpdateOutput(); };
    }

    if (auto* cb = FindWidget<Checkbox>("feature-check")) {
        _checked = cb->Checked;
        cb->OnChanged = [this](const bool v) { _checked = v; UpdateOutput(); };
    }

    if (auto* rg = FindWidget<RadioGroup>("difficulty-radio")) {
        _radioIdx = rg->SelectedIndex;
        rg->OnChanged = [this](const i32 idx) { _radioIdx = idx; UpdateOutput(); };
    }

    if (auto* sw = FindWidget<Switch>("autosave-switch")) {
        _switchOn = sw->On;
        sw->OnChanged = [this](const bool v) { _switchOn = v; UpdateOutput(); };
    }
}

void WidgetTestScreen::UpdateOutput()
{
    auto* out = FindWidget<Label>("output");
    if (!out) return;

    static const char* kQualities[] = { "Low", "Medium", "High", "Ultra" };
    static const char* kDiffs[]     = { "Easy", "Normal", "Hard" };

    const std::string_view quality =
        (_dropIdx >= 0 && _dropIdx < 4) ? kQualities[_dropIdx] : "?";
    const std::string_view diff =
        (_radioIdx >= 0 && _radioIdx < 3) ? kDiffs[_radioIdx] : "?";

    out->Text = std::format(
        "Text: \"{}\"   Number: {}   Volume: {:.0f}%\n"
        "Quality: {}   Feature: {}   Difficulty: {}   Auto-Save: {}",
        _textVal.empty() ? "(empty)" : _textVal,
        _numStr.empty()  ? "(empty)" : _numStr,
        _sliderVal * 100.0f,
        quality,
        _checked   ? "ON" : "OFF",
        diff,
        _switchOn  ? "ON" : "OFF");
}

} // namespace Arcbit
