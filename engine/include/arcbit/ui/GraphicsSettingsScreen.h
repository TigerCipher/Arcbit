#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>

namespace Arcbit
{
class Button;
class Label;

// ---------------------------------------------------------------------------
// GraphicsSettingsScreen — toggles for Fullscreen, VSync, and FPS limit.
//
// FPS limit cycles through common presets. VSync and FPS changes persist to
// Settings immediately; Fullscreen fires OnToggleFullscreen so the caller
// (Application) can perform the window swap. Wire OnBack to pop the screen.
// ---------------------------------------------------------------------------
class GraphicsSettingsScreen : public UIScreen
{
public:
    std::function<void()> OnBack;
    std::function<void()> OnToggleFullscreen; // must call Application::ToggleFullscreen

    GraphicsSettingsScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;

private:
    Button* _fullscreenBtn = nullptr;
    Button* _vsyncBtn      = nullptr;
    Label*  _fpsLabel      = nullptr;

    void AddToggleRow(UIWidget* parent, const char* name, f32 yOffset,
                      Button*& outBtn, std::function<bool()> get,
                      std::function<void()> toggle);
    void AddFpsRow(UIWidget* parent, f32 yOffset);
    void CycleFps(int dir);
};

} // namespace Arcbit
