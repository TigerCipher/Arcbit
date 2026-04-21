#include <algorithm>
#include <arcbit/ui/AudioSettingsScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/audio/AudioManager.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/core/Loc.h>

#include <format>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string PctText(const f32 v)
{
    return std::format("{}%", static_cast<i32>(v * 100.0f + 0.5f));
}

static Color DimText() { return {0.65f, 0.65f, 0.65f, 1.0f}; }

// ---------------------------------------------------------------------------
// AddVolumeRow — label + ◄ + progress bar + ► + percentage
// ---------------------------------------------------------------------------

void AudioSettingsScreen::AddVolumeRow(UIWidget* parent, const char* name, const f32 yOffset,
                                        ProgressBar*& outBar, Label*& outPct,
                                        std::function<f32()> get, std::function<void(f32)> set)
{
    const f32 labelX = 20.0f;
    const f32 decX   = 168.0f;
    const f32 barX   = 202.0f;
    const f32 incX   = 342.0f;
    const f32 pctX   = 376.0f;
    const f32 h      = 28.0f;

    auto* lbl    = parent->AddChild<Label>();
    lbl->Text    = name;
    lbl->Size    = {144.0f, h};
    lbl->Anchor  = {0.0f, 0.0f};
    lbl->Offset  = {labelX, yOffset};
    lbl->ZOrder  = 2;

    auto* dec      = parent->AddChild<Button>();
    dec->Text      = "<";
    dec->Size      = {28.0f, h};
    dec->Anchor    = {0.0f, 0.0f};
    dec->Offset    = {decX, yOffset};
    dec->ZOrder    = 2;
    dec->OnClick   = [get, set, &outBar, &outPct]() {
        const f32 v = std::clamp(get() - 0.1f, 0.0f, 1.0f);
        set(v);
        if (outBar) outBar->Value = v;
        if (outPct) outPct->Text  = PctText(v);
    };

    auto* bar    = parent->AddChild<ProgressBar>();
    bar->Value   = get();
    bar->Size    = {136.0f, h};
    bar->Anchor  = {0.0f, 0.0f};
    bar->Offset  = {barX, yOffset};
    bar->ZOrder  = 2;
    outBar = bar;

    auto* inc      = parent->AddChild<Button>();
    inc->Text      = ">";
    inc->Size      = {28.0f, h};
    inc->Anchor    = {0.0f, 0.0f};
    inc->Offset    = {incX, yOffset};
    inc->ZOrder    = 2;
    inc->OnClick   = [get, set, &outBar, &outPct]() {
        const f32 v = std::clamp(get() + 0.1f, 0.0f, 1.0f);
        set(v);
        if (outBar) outBar->Value = v;
        if (outPct) outPct->Text  = PctText(v);
    };

    auto* pct    = parent->AddChild<Label>();
    pct->Text    = PctText(get());
    pct->Size    = {44.0f, h};
    pct->Anchor  = {0.0f, 0.0f};
    pct->Offset  = {pctX, yOffset};
    pct->ZOrder  = 2;
    outPct = pct;
}

// ---------------------------------------------------------------------------
// OnEnter
// ---------------------------------------------------------------------------

void AudioSettingsScreen::OnEnter()
{
    _roots.clear();

    Add<Overlay>();

    const f32 panelW = 460.0f;
    const f32 panelH = 300.0f;

    auto* bg   = Add<Panel>();
    bg->Size   = {panelW, panelH};
    bg->Anchor = {0.5f, 0.5f};
    bg->Pivot  = {0.5f, 0.5f};
    bg->ZOrder = 1;

    auto* title   = bg->AddChild<Label>();
    title->Text   = Loc::Get("ui.audio.title");
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

    AddVolumeRow(bg, Loc::Get("ui.audio.master").c_str(), 72.0f, _masterBar, _masterPct,
        []()      { return Settings::Audio.MasterVolume; },
        [](f32 v) { Settings::Audio.MasterVolume = v; Settings::MarkDirty(); AudioManager::SetMasterVolume(v); });

    AddVolumeRow(bg, Loc::Get("ui.audio.music").c_str(), 120.0f, _musicBar, _musicPct,
        []()      { return Settings::Audio.MusicVolume; },
        [](f32 v) { Settings::Audio.MusicVolume = v; Settings::MarkDirty(); AudioManager::SetMusicVolume(v); });

    AddVolumeRow(bg, Loc::Get("ui.audio.sfx").c_str(), 168.0f, _sfxBar, _sfxPct,
        []()      { return Settings::Audio.SfxVolume; },
        [](f32 v) { Settings::Audio.SfxVolume = v; Settings::MarkDirty(); AudioManager::SetSfxVolume(v); });

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
