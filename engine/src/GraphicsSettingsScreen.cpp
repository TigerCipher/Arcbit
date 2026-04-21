#include <arcbit/ui/GraphicsSettingsScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/core/Loc.h>

#include <algorithm>
#include <format>

namespace Arcbit
{

static constexpr u32 FpsPresets[] = {0, 30, 60, 120, 144, 165, 240};
static constexpr i32 FpsPresetCount = static_cast<i32>(std::size(FpsPresets));

static std::string FpsPresetText(const u32 limit)
{
    return limit == 0 ? std::string(Loc::Get("ui.common.unlimited"))
                      : std::format("{} fps", limit);
}

// ---------------------------------------------------------------------------
// AddToggleRow — label on the left, toggle button on the right
// ---------------------------------------------------------------------------

void GraphicsSettingsScreen::AddToggleRow(UIWidget* parent, const char* name, const f32 yOffset,
                                           Button*& outBtn,
                                           std::function<bool()> get, std::function<void()> toggle)
{
    auto* lbl    = parent->AddChild<Label>();
    lbl->Text    = name;
    lbl->Size    = {200.0f, 32.0f};
    lbl->Anchor  = {0.0f, 0.0f};
    lbl->Offset  = {20.0f, yOffset};
    lbl->ZOrder  = 2;

    auto* btn    = parent->AddChild<Button>();
    btn->Text    = get() ? Loc::Get("ui.common.on") : Loc::Get("ui.common.off");
    btn->Size    = {120.0f, 32.0f};
    btn->Anchor  = {0.0f, 0.0f};
    btn->Offset  = {240.0f, yOffset};
    btn->ZOrder  = 2;
    btn->OnClick = [get, toggle, &outBtn]() {
        toggle();
        if (outBtn) outBtn->Text = get() ? Loc::Get("ui.common.on") : Loc::Get("ui.common.off");
    };
    outBtn = btn;
}

// ---------------------------------------------------------------------------
// AddFpsRow — label + ◄ + value label + ►
// ---------------------------------------------------------------------------

void GraphicsSettingsScreen::AddFpsRow(UIWidget* parent, const f32 yOffset)
{
    auto* lbl    = parent->AddChild<Label>();
    lbl->Text    = Loc::Get("ui.graphics.fps_limit");
    lbl->Size    = {200.0f, 32.0f};
    lbl->Anchor  = {0.0f, 0.0f};
    lbl->Offset  = {20.0f, yOffset};
    lbl->ZOrder  = 2;

    auto* dec    = parent->AddChild<Button>();
    dec->Text    = "<";
    dec->Size    = {32.0f, 32.0f};
    dec->Anchor  = {0.0f, 0.0f};
    dec->Offset  = {240.0f, yOffset};
    dec->ZOrder  = 2;
    dec->OnClick = [this]() { CycleFps(-1); };

    auto* val    = parent->AddChild<Label>();
    val->Text    = FpsPresetText(Settings::Graphics.FpsLimit);
    val->Align   = TextAlign::Center;
    val->Size    = {90.0f, 32.0f};
    val->Anchor  = {0.0f, 0.0f};
    val->Offset  = {278.0f, yOffset};
    val->ZOrder  = 2;
    _fpsLabel    = val;

    auto* inc    = parent->AddChild<Button>();
    inc->Text    = ">";
    inc->Size    = {32.0f, 32.0f};
    inc->Anchor  = {0.0f, 0.0f};
    inc->Offset  = {374.0f, yOffset};
    inc->ZOrder  = 2;
    inc->OnClick = [this]() { CycleFps(1); };
}

void GraphicsSettingsScreen::CycleFps(const int dir)
{
    // Find closest preset index, then advance.
    i32 idx = 0;
    for (i32 i = 0; i < FpsPresetCount; ++i)
        if (FpsPresets[i] == Settings::Graphics.FpsLimit) { idx = i; break; }

    idx = (idx + dir + FpsPresetCount) % FpsPresetCount;
    Settings::Graphics.FpsLimit = FpsPresets[idx];
    Settings::MarkDirty();

    if (_fpsLabel) _fpsLabel->Text = FpsPresetText(Settings::Graphics.FpsLimit);
}

// ---------------------------------------------------------------------------
// OnEnter
// ---------------------------------------------------------------------------

void GraphicsSettingsScreen::OnEnter()
{
    _roots.clear();

    Add<Overlay>();

    const f32 panelW = 420.0f;
    const f32 panelH = 300.0f;

    auto* bg   = Add<Panel>();
    bg->Size   = {panelW, panelH};
    bg->Anchor = {0.5f, 0.5f};
    bg->Pivot  = {0.5f, 0.5f};
    bg->ZOrder = 1;

    auto* title   = bg->AddChild<Label>();
    title->Text   = Loc::Get("ui.graphics.title");
    title->Align  = TextAlign::Center;
    title->Size   = {panelW, 28.0f};
    title->Anchor = {0.5f, 0.0f};
    title->Pivot  = {0.5f, 0.0f};
    title->Offset = {0.0f, 16.0f};
    title->ZOrder = 2;

    auto* sep   = bg->AddChild<Panel>();
    sep->Size   = {panelW - 40.0f, 1.0f};
    sep->Anchor = {0.5f, 0.0f};
    sep->Pivot  = {0.5f, 0.0f};
    sep->Offset = {0.0f, 56.0f};
    sep->ZOrder = 2;

    AddToggleRow(bg, Loc::Get("ui.graphics.fullscreen").c_str(), 70.0f, _fullscreenBtn,
        []()  { return Settings::Graphics.Fullscreen; },
        [this]() {
            if (OnToggleFullscreen) OnToggleFullscreen();
            // Settings::Graphics.Fullscreen is flipped by Application::ToggleFullscreen
        });

    AddToggleRow(bg, Loc::Get("ui.graphics.vsync").c_str(), 118.0f, _vsyncBtn,
        []()  { return Settings::Graphics.VSync; },
        []()  { Settings::Graphics.VSync = !Settings::Graphics.VSync; Settings::MarkDirty(); });

    AddFpsRow(bg, 166.0f);

    auto* note    = bg->AddChild<Label>();
    note->Text    = Loc::Get("ui.graphics.vsync_note");
    note->Align   = TextAlign::Center;
    note->Size    = {panelW, 18.0f};
    note->Anchor  = {0.5f, 0.0f};
    note->Pivot   = {0.5f, 0.0f};
    note->Offset  = {0.0f, 214.0f};
    note->ZOrder  = 2;
    note->TextColor = {0.50f, 0.50f, 0.50f, 1.0f};

    auto* back      = bg->AddChild<Button>();
    back->Text      = Loc::Get("ui.common.back");
    back->Size      = {160.0f, 40.0f};
    back->Anchor    = {0.5f, 1.0f};
    back->Pivot     = {0.5f, 1.0f};
    back->Offset    = {0.0f, -16.0f};
    back->Focusable = true;
    back->ZOrder    = 2;
    back->OnClick   = [this] { if (OnBack) OnBack(); };
}

} // namespace Arcbit
