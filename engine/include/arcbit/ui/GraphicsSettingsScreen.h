#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>

namespace Arcbit
{
class Button;
class Label;
class Switch;

// ---------------------------------------------------------------------------
// GraphicsSettingsScreen — toggles for Fullscreen, VSync, FPS limit, Show
// FPS, and Show Debug Info.
//
// VSync, Show FPS, and Show Debug Info are Switch widgets that update
// Settings::Graphics directly. Fullscreen fires OnToggleFullscreen.
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
    void OnBackPressed() override;

private:
    Button* _fullscreenBtn  = nullptr;
    Switch* _vsyncSwitch    = nullptr;
    Switch* _showFpsSwitch  = nullptr;
    Switch* _showDebugSwitch= nullptr;
    Label*  _fpsLabel       = nullptr;

    void CycleFps(int dir);
};

} // namespace Arcbit
