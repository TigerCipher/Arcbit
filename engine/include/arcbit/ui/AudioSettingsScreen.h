#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>

namespace Arcbit
{
class ProgressBar;
class Label;

// ---------------------------------------------------------------------------
// AudioSettingsScreen — sliders for Master, Music, and SFX volume.
//
// Adjusts AudioManager volumes and Settings::Audio in real-time as the user
// clicks the ◄/► buttons. Wire OnBack to pop the screen.
// ---------------------------------------------------------------------------
class AudioSettingsScreen : public UIScreen
{
public:
    std::function<void()> OnBack;

    AudioSettingsScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;

private:
    ProgressBar* _masterBar = nullptr;
    ProgressBar* _musicBar  = nullptr;
    ProgressBar* _sfxBar    = nullptr;
    Label*       _masterPct = nullptr;
    Label*       _musicPct  = nullptr;
    Label*       _sfxPct    = nullptr;

    void AddVolumeRow(UIWidget* parent, const char* name, f32 yOffset,
                      ProgressBar*& outBar, Label*& outPct,
                      std::function<f32()> get, std::function<void(f32)> set);
};

} // namespace Arcbit
