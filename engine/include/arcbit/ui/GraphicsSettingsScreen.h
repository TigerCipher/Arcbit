#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>

namespace Arcbit
{
class Button;
class Label;

// ---------------------------------------------------------------------------
// GraphicsSettingsScreen — toggles for Fullscreen, VSync, and FPS limit.
//
// Visual layout is loaded from LayoutPath (.arcui); callbacks are wired in
// OnEnter via FindWidget. FPS limit cycles through common presets. Fullscreen
// fires OnToggleFullscreen so the caller can perform the window swap.
// Wire OnBack to pop the screen.
// ---------------------------------------------------------------------------
class GraphicsSettingsScreen : public UIScreen
{
public:
    std::function<void()> OnBack;
    std::function<void()> OnToggleFullscreen; // must call Application::ToggleFullscreen
    std::string LayoutPath = "assets/engine/ui/graphics_settings.arcui";

    GraphicsSettingsScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;

private:
    Button* _fullscreenBtn = nullptr;
    Button* _vsyncBtn      = nullptr;
    Label*  _fpsLabel      = nullptr;

    void CycleFps(int dir);
};

} // namespace Arcbit
