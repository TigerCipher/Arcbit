#pragma once

#include <arcbit/ui/UIScreen.h>
#include <arcbit/render/RenderHandle.h>

#include <functional>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// PauseMenuScreen — engine-provided pause menu.
//
// Set PanelTexture + PanelSampler for a NineSlice background, or leave them
// invalid to fall back to a solid Panel.  Wire OnResume / OnQuit before Push.
// OnSettings defaults to a no-op if unset; the engine does not hard-link to
// InputRebindScreen so games can substitute their own settings screen.
// ---------------------------------------------------------------------------
class PauseMenuScreen : public UIScreen
{
public:
    TextureHandle PanelTexture;
    SamplerHandle PanelSampler;

    std::function<void()> OnResume;   // called by "Resume" button
    std::function<void()> OnSettings; // called by "Settings" button; hide with ShowSettings=false
    std::function<void()> OnQuit;     // called by "Quit" button

    bool ShowSettings = true;

    PauseMenuScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;
};
} // namespace Arcbit
