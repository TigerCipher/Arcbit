#pragma once

#include <arcbit/ui/UIScreen.h>

#include <functional>
#include <string>
#include <string_view>

namespace Arcbit
{

// ---------------------------------------------------------------------------
// AudioSettingsScreen — sliders for Master, Music, and SFX volume.
//
// Visual layout is loaded from LayoutPath (.arcui); callbacks are wired in
// OnEnter via FindWidget. Adjusts AudioManager volumes and Settings::Audio in
// real-time as the user clicks the ◄/► buttons. Wire OnBack to pop the screen.
// ---------------------------------------------------------------------------
class AudioSettingsScreen : public UIScreen
{
public:
    std::function<void()> OnBack;
    std::string LayoutPath = "assets/engine/ui/audio_settings.arcui";

    AudioSettingsScreen() { BlocksInput = true; TransitionSpeed = 6.0f; }

    void OnEnter() override;

private:
    void WireVolumeRow(std::string_view prefix,
                       std::function<f32()> get, std::function<void(f32)> set);
};

} // namespace Arcbit
