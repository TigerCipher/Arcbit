#include <arcbit/ui/InputRebindScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/input/InputManager.h>

#include <algorithm>
#include <format>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool HasHiddenPrefix(const std::string_view name,
                             const std::vector<std::string>& prefixes)
{
    for (const auto& p : prefixes)
        if (name.starts_with(p)) return true;
    return false;
}

// Key and mouse button bindings for the left column.
static std::string AllKBMBindings(const InputManager& input, const ActionID action)
{
    std::string result;
    for (const Binding& b : input.GetBindings(action)) {
        if (b.BindingType != Binding::Type::Key &&
            b.BindingType != Binding::Type::MouseButton) continue;
        if (!result.empty()) result += ", ";
        result += InputManager::BindingToString(b);
    }
    return result.empty() ? "---" : result;
}

static std::string AllGamepadBindings(const InputManager& input, const ActionID action)
{
    std::string result;
    for (const Binding& b : input.GetBindings(action)) {
        if (b.BindingType != Binding::Type::GamepadButton) continue;
        if (!result.empty()) result += ", ";
        result += InputManager::BindingToString(b);
    }
    return result.empty() ? "---" : result;
}

// ---------------------------------------------------------------------------
// OnEnter — build the full screen widget tree
// ---------------------------------------------------------------------------

void InputRebindScreen::OnEnter()
{
    _roots.clear();
    _rows.clear();
    _listeningIdx = -1;
    _flashTimer   = 0.0f;

    if (!Input) return;

    // Fully opaque scrim so the screen below isn't visible.
    auto* scrim            = Add<Panel>();
    scrim->SizePercent     = {1.0f, 1.0f};
    scrim->BackgroundColor = {0.04f, 0.04f, 0.06f, 1.0f};
    scrim->ZOrder          = 0;

    const f32 panelW = 680.0f;
    const f32 panelH = 540.0f;

    auto* bg   = Add<Panel>();
    bg->Size   = {panelW, panelH};
    bg->Anchor = {0.5f, 0.5f};
    bg->Pivot  = {0.5f, 0.5f};
    bg->ZOrder = 1;

    auto* title   = bg->AddChild<Label>();
    title->Text   = "Input Bindings";
    title->Align  = TextAlign::Center;
    title->Size   = {panelW, 28.0f};
    title->Anchor = {0.5f, 0.0f};
    title->Pivot  = {0.5f, 0.0f};
    title->Offset = {0.0f, 16.0f};
    title->ZOrder = 2;

    // Column headers
    auto* colAction   = bg->AddChild<Label>();
    colAction->Text   = "Action";
    colAction->Size   = {220.0f, 18.0f};
    colAction->Anchor = {0.0f, 0.0f};
    colAction->Offset = {20.0f, 54.0f};
    colAction->ZOrder = 2;

    auto* colKey   = bg->AddChild<Label>();
    colKey->Text   = "Key / Mouse";
    colKey->Size   = {170.0f, 18.0f};
    colKey->Anchor = {0.0f, 0.0f};
    colKey->Offset = {260.0f, 54.0f};
    colKey->ZOrder = 2;

    auto* colCtrl   = bg->AddChild<Label>();
    colCtrl->Text   = "Controller";
    colCtrl->Size   = {170.0f, 18.0f};
    colCtrl->Anchor = {0.0f, 0.0f};
    colCtrl->Offset = {450.0f, 54.0f};
    colCtrl->ZOrder = 2;

    auto* sep    = bg->AddChild<Panel>();
    sep->Size    = {panelW - 40.0f, 1.0f};
    sep->Anchor  = {0.5f, 0.0f};
    sep->Pivot   = {0.5f, 0.0f};
    sep->Offset  = {0.0f, 76.0f};
    sep->ZOrder  = 2;

    _scroll                 = bg->AddChild<ScrollPanel>();
    _scroll->Size           = {panelW - 20.0f, panelH - 148.0f};
    _scroll->Anchor         = {0.5f, 0.0f};
    _scroll->Pivot          = {0.5f, 0.0f};
    _scroll->Offset         = {0.0f, 82.0f};
    _scroll->ZOrder         = 2;
    _scroll->ScrollbarWidth = 8.0f;

    RebuildRows();

    auto* back      = bg->AddChild<Button>();
    back->Text      = "Back";
    back->Size      = {160.0f, 40.0f};
    back->Anchor    = {0.5f, 1.0f};
    back->Pivot     = {0.5f, 1.0f};
    back->Offset    = {0.0f, -16.0f};
    back->Focusable = true;
    back->ZOrder    = 2;
    back->OnClick   = OnBack;
}

// ---------------------------------------------------------------------------
// RebuildRows — populate _scroll grouped by category
// ---------------------------------------------------------------------------

void InputRebindScreen::RebuildRows()
{
    if (!Input || !_scroll) return;
    _rows.clear();

    struct Entry { ActionID id; std::string_view name, display, category; };
    std::vector<Entry> entries;
    for (const ActionID action : Input->GetAllActions()) {
        const std::string_view name = Input->GetActionName(action);
        if (name.empty() || HasHiddenPrefix(name, HiddenPrefixes)) continue;
        entries.push_back({action, name,
                           Input->GetActionDisplayName(action),
                           Input->GetActionCategory(action)});
    }
    std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
        return a.category != b.category ? a.category < b.category
                                        : a.display   < b.display;
    });

    const f32 rowH    = 36.0f;
    const f32 catH    = 24.0f;
    const f32 rowW    = _scroll->Size.X - _scroll->ScrollbarWidth - 8.0f;
    const f32 keyColX = 242.0f;
    const f32 ctrlColX = 432.0f;
    const f32 btnW    = 160.0f;
    f32       y       = 4.0f;
    std::string_view lastCategory;

    for (const Entry& e : entries) {
        if (e.category != lastCategory) {
            if (!e.category.empty()) {
                auto* cat      = _scroll->AddChild<Label>();
                cat->Text      = std::string(e.category);
                cat->Size      = {rowW, catH};
                cat->Anchor    = {0.0f, 0.0f};
                cat->Offset    = {8.0f, y + 4.0f};
                cat->ZOrder    = 3;
                y += catH;
            }
            lastCategory = e.category;
        }

        if (!_rows.empty()) {
            auto* sep   = _scroll->AddChild<Panel>();
            sep->Size   = {rowW - 8.0f, 1.0f};
            sep->Anchor = {0.0f, 0.0f};
            sep->Offset = {4.0f, y};
            sep->ZOrder = 3;
        }

        ActionRow& row = _rows.emplace_back();
        row.action     = e.id;

        row.nameLabel         = _scroll->AddChild<Label>();
        row.nameLabel->Text   = std::string(e.display);
        row.nameLabel->Size   = {230.0f, rowH};
        row.nameLabel->Anchor = {0.0f, 0.0f};
        row.nameLabel->Offset = {16.0f, y};
        row.nameLabel->ZOrder = 4;

        const i32 rowIdx = static_cast<i32>(_rows.size()) - 1;

        row.keyButton          = _scroll->AddChild<Button>();
        row.keyButton->Size    = {btnW, rowH - 6.0f};
        row.keyButton->Anchor  = {0.0f, 0.0f};
        row.keyButton->Offset  = {keyColX, y + 3.0f};
        row.keyButton->Focusable = true;
        row.keyButton->ZOrder  = 4;
        row.keyButton->OnClick = [this, rowIdx] { StartListening(rowIdx, true); };

        row.ctrlButton          = _scroll->AddChild<Button>();
        row.ctrlButton->Size    = {btnW, rowH - 6.0f};
        row.ctrlButton->Anchor  = {0.0f, 0.0f};
        row.ctrlButton->Offset  = {ctrlColX, y + 3.0f};
        row.ctrlButton->Focusable = true;
        row.ctrlButton->ZOrder  = 4;
        row.ctrlButton->OnClick = [this, rowIdx] { StartListening(rowIdx, false); };

        RefreshKeyText(row);
        RefreshCtrlText(row);

        y += rowH;
    }

    _scroll->ContentHeight = y + 4.0f;
    _scroll->ScrollOffset  = 0.0f;
}

// ---------------------------------------------------------------------------
// Listening mode
// ---------------------------------------------------------------------------

void InputRebindScreen::StartListening(const i32 idx, const bool forKey)
{
    // Clicking the already-listening button cancels.
    if (_listeningIdx == idx && _rows[idx].listenKey == forKey) {
        StopListening();
        return;
    }

    StopListening();
    if (idx < 0 || idx >= static_cast<i32>(_rows.size())) return;

    _listeningIdx            = idx;
    _rows[idx].listening     = true;
    _rows[idx].listenKey     = forKey;

    if (forKey) _rows[idx].keyButton->Text  = "...";
    else        _rows[idx].ctrlButton->Text = "...";
}

void InputRebindScreen::StopListening()
{
    if (_listeningIdx >= 0 && _listeningIdx < static_cast<i32>(_rows.size())) {
        ActionRow& row  = _rows[_listeningIdx];
        row.listening   = false;
        RefreshKeyText(row);
        RefreshCtrlText(row);
    }
    _listeningIdx = -1;
    _flashTimer   = 0.0f;
}

void InputRebindScreen::RefreshKeyText(ActionRow& row) const
{
    row.keyButton->Text = AllKBMBindings(*Input, row.action);
}

void InputRebindScreen::RefreshCtrlText(ActionRow& row) const
{
    row.ctrlButton->Text = AllGamepadBindings(*Input, row.action);
}

// ---------------------------------------------------------------------------
// OnTick — capture input while listening
// ---------------------------------------------------------------------------

void InputRebindScreen::OnTick(const f32 dt, const InputManager& input)
{
    if (_listeningIdx < 0) return;

    // Flash the active button.
    _flashTimer += dt;
    const bool flash = static_cast<i32>(_flashTimer * 3.0f) % 2 == 0;
    if (_listeningIdx < static_cast<i32>(_rows.size())) {
        ActionRow& row = _rows[_listeningIdx];
        const std::string blinkText = flash ? "..." : "   ";
        if (row.listenKey) row.keyButton->Text  = blinkText;
        else               row.ctrlButton->Text = blinkText;
    }

    ActionRow& row = _rows[_listeningIdx];

    if (row.listenKey) {
        // Key/mouse column — capture whichever arrives first.
        const Key         k   = input.GetAnyJustPressedKey();
        const MouseButton mb  = input.GetAnyJustPressedMouseButton();

        if (k == Key::Unknown && mb == MouseButton::Count) return;

        Input->ClearKBMBindings(row.action);
        if (k != Key::Unknown) {
            Input->UnbindKey(k);
            Input->BindKey(row.action, k);
        } else {
            Input->UnbindMouseButton(mb);
            Input->BindMouseButton(row.action, mb);
        }
    } else {
        const GamepadButton btn = input.GetAnyJustPressedGamepadButton();
        if (btn == GamepadButton::Count) return;

        Input->UnbindGamepadButton(btn);
        Input->ClearGamepadBindings(row.action);
        Input->BindGamepadButton(row.action, btn);
    }

    StopListening();
}

} // namespace Arcbit
