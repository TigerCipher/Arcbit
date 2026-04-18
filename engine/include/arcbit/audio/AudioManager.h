#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>

#include <string_view>

namespace Arcbit {

// ---------------------------------------------------------------------------
// AudioManager
//
// Static facade over miniaudio. Handles device init/shutdown, background music
// streaming, fire-and-forget SFX, and spatial point sounds (driven by AudioSystem).
//
// Volume hierarchy:
//   effective SFX volume   = MasterVolume * SfxVolume
//   effective music volume = MasterVolume * MusicVolume
//
// Call Init() at startup (after Settings::Init) and Shutdown() before the
// program exits. All other methods are safe to call from game code.
// ---------------------------------------------------------------------------
class AudioManager
{
public:
    // Call once at startup, after Settings::Init(). Reads initial volumes from
    // Settings::Audio automatically.
    static void Init();

    // Stop all playback and release the audio device.
    static void Shutdown();

    // -----------------------------------------------------------------------
    // SFX — fire-and-forget
    // -----------------------------------------------------------------------

    // Play a short sound effect at the given volume [0, 1], multiplied by
    // SfxVolume. miniaudio cleans up the sound when it finishes — no handle needed.
    static void PlayOneShot(std::string_view path, f32 volume = 1.0f);

    // -----------------------------------------------------------------------
    // Music
    // -----------------------------------------------------------------------

    // Stream a music file. Stops any currently playing track first.
    static void PlayMusic(std::string_view path, bool loop = true);

    // Stop the current music. No-op if nothing is playing.
    static void StopMusic();

    static void PauseMusic();
    static void ResumeMusic();

    // -----------------------------------------------------------------------
    // Volume
    // -----------------------------------------------------------------------

    static void SetMasterVolume(f32 volume); // [0, 1]
    static void SetMusicVolume(f32 volume);
    static void SetSfxVolume(f32 volume);

    // Read MasterVolume / MusicVolume / SfxVolume from Settings::Audio and apply.
    static void ApplySettings();

    // -----------------------------------------------------------------------
    // Spatial sounds — called by AudioSystem
    // -----------------------------------------------------------------------

    // Move the audio listener to the given world position each tick.
    // AudioSystem calls this automatically with the camera position.
    static void SetListenerPosition(Vec2 position);

    // Create a looping or one-shot spatial sound at the given world position.
    // Returns an opaque handle; caller must call DestroySpatialSound when done.
    // Returns nullptr if the file cannot be opened.
    [[nodiscard]] static void* CreateSpatialSound(std::string_view path, bool loop, f32 volume);

    // Update position and max-hearing radius each tick.
    static void SetSpatialSoundPosition(void* handle, Vec2 position, f32 radius);

    // Stop playback and free the handle returned by CreateSpatialSound.
    static void DestroySpatialSound(void* handle);
};

} // namespace Arcbit
