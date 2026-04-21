#include <arcbit/ui/PauseMenuScreen.h>
#include <arcbit/ui/Widgets.h>

namespace Arcbit
{
void PauseMenuScreen::OnEnter()
{
    _roots.clear();

    Add<Overlay>();

    const i32 btnCount = 2 // Resume + Quit always shown
        + (ShowControls         ? 1 : 0)
        + (ShowAudioSettings    ? 1 : 0)
        + (ShowGraphicsSettings ? 1 : 0);

    const f32 panelW = 360.0f;
    const f32 panelH = 80.0f + static_cast<f32>(btnCount) * 56.0f + 20.0f;

    UIWidget* bg;
    if (PanelTexture.IsValid()) {
        auto* ns         = Add<NineSlice>();
        ns->Texture      = PanelTexture;
        ns->Sampler      = PanelSampler;
        ns->UVBorderLeft = ns->UVBorderRight  = 0.125f;
        ns->UVBorderTop  = ns->UVBorderBottom = 0.125f;
        ns->PixelLeft    = ns->PixelRight     = 16.0f;
        ns->PixelTop     = ns->PixelBottom    = 16.0f;
        bg               = ns;
    }
    else { bg = Add<Panel>(); }
    bg->Size   = {panelW, panelH};
    bg->Anchor = {0.5f, 0.5f};
    bg->Pivot  = {0.5f, 0.5f};
    bg->ZOrder = 1;

    auto* title   = bg->AddChild<Label>();
    title->Text   = "PAUSED";
    title->Align  = TextAlign::Center;
    title->Size   = {panelW, 32.0f};
    title->Anchor = {0.5f, 0.0f};
    title->Pivot  = {0.5f, 0.0f};
    title->Offset = {0.0f, 20.0f};
    title->ZOrder = 2;

    auto* sep   = bg->AddChild<Panel>();
    sep->Size   = {panelW - 40.0f, 1.0f};
    sep->Anchor = {0.5f, 0.0f};
    sep->Pivot  = {0.5f, 0.0f};
    sep->Offset = {0.0f, 62.0f};
    sep->ZOrder = 2;

    const f32 btnW = 240.0f;
    const f32 btnH = 44.0f;
    const f32 gap  = 56.0f;

    auto MakeButton = [&](const char* label, f32 yOffset, std::function<void()> cb,
                          Color textColor = {0, 0, 0, 0}) {
        auto* btn      = bg->AddChild<Button>();
        btn->Text      = label;
        btn->Size      = {btnW, btnH};
        btn->Anchor    = {0.5f, 0.0f};
        btn->Pivot     = {0.5f, 0.0f};
        btn->Offset    = {0.0f, yOffset};
        btn->Focusable = true;
        btn->ZOrder    = 2;
        btn->TextColor = textColor;
        btn->OnClick   = std::move(cb);
    };

    f32 y = 72.0f;
    MakeButton("Resume",   y, OnResume); y += gap;
    if (ShowControls)         { MakeButton("Controls", y, OnControls);         y += gap; }
    if (ShowAudioSettings)    { MakeButton("Audio",    y, OnAudioSettings);    y += gap; }
    if (ShowGraphicsSettings) { MakeButton("Graphics", y, OnGraphicsSettings); y += gap; }
    MakeButton("Quit", y, OnQuit, {0.88f, 0.30f, 0.30f, 1.0f});
}
} // namespace Arcbit
