#include <arcbit/ui/PauseMenuScreen.h>
#include <arcbit/ui/Widgets.h>

namespace Arcbit
{

void PauseMenuScreen::OnEnter()
{
    _roots.clear();

    // Full-screen dark scrim
    auto* scrim          = Add<Panel>();
    scrim->SizePercent   = {1.0f, 1.0f};
    scrim->Anchor        = {0.0f, 0.0f};
    scrim->ZOrder        = 0;
    scrim->DrawBorder    = false;

    // Center panel — NineSlice if a texture is wired, else solid Panel
    const f32 panelW   = 360.0f;
    const f32 btnCount = ShowSettings ? 3.0f : 2.0f;
    const f32 panelH   = 80.0f + btnCount * 56.0f + 20.0f;

    UIWidget* bg;
    if (PanelTexture.IsValid()) {
        auto* ns         = Add<NineSlice>();
        ns->Texture      = PanelTexture;
        ns->Sampler      = PanelSampler;
        ns->UVBorderLeft = ns->UVBorderRight  = 0.125f;
        ns->UVBorderTop  = ns->UVBorderBottom = 0.125f;
        ns->PixelLeft    = ns->PixelRight     = 16.0f;
        ns->PixelTop     = ns->PixelBottom    = 16.0f;
        bg = ns;
    } else {
        bg = Add<Panel>();
    }
    bg->Size   = {panelW, panelH};
    bg->Anchor = {0.5f, 0.5f};
    bg->Pivot  = {0.5f, 0.5f};
    bg->ZOrder = 1;

    auto* title      = bg->AddChild<Label>();
    title->Text      = "PAUSED";
    title->Align     = TextAlign::Center;
    title->Size      = {panelW, 32.0f};
    title->Anchor    = {0.5f, 0.0f};
    title->Pivot     = {0.5f, 0.0f};
    title->Offset    = {0.0f, 20.0f};
    title->ZOrder    = 2;

    // Thin separator below title — Panel uses skin.PanelBg (dark), visible on light textures.
    auto* sep    = bg->AddChild<Panel>();
    sep->Size    = {panelW - 40.0f, 1.0f};
    sep->Anchor  = {0.5f, 0.0f};
    sep->Pivot   = {0.5f, 0.0f};
    sep->Offset  = {0.0f, 62.0f};
    sep->ZOrder  = 2;

    const f32 btnW  = 240.0f;
    const f32 btnH  = 44.0f;
    const f32 btnX0 = 72.0f;   // top of first button from panel top
    const f32 gap   = 56.0f;

    auto MakeButton = [&](const char* label, f32 yOffset, std::function<void()> cb,
                          Color textColor = {0,0,0,0}) -> Button*
    {
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
        return btn;
    };

    f32 y = btnX0;
    MakeButton("Resume",   y, OnResume);   y += gap;
    if (ShowSettings)
        MakeButton("Settings", y, OnSettings); y += gap;
    MakeButton("Quit",     y, OnQuit, {0.88f, 0.30f, 0.30f, 1.0f});
}

} // namespace Arcbit
