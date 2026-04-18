// MINIAUDIO_IMPLEMENTATION must be defined in exactly one translation unit.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <arcbit/audio/AudioManager.h>
#include <arcbit/settings/Settings.h>
#include <arcbit/core/Log.h>

namespace Arcbit {
namespace {

ma_engine      g_engine;
ma_sound_group g_sfxGroup;
ma_sound_group g_musicGroup;
ma_sound       g_musicSound;
bool           g_initialized = false;
bool           g_musicLoaded = false;

f32 g_masterVolume = 1.0f;
f32 g_musicVolume  = 1.0f;
f32 g_sfxVolume    = 1.0f;

void ApplyGroupVolumes()
{
    ma_sound_group_set_volume(&g_sfxGroup,   g_masterVolume * g_sfxVolume);
    ma_sound_group_set_volume(&g_musicGroup, g_masterVolume * g_musicVolume);
}

} // namespace

void AudioManager::Init()
{
    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &g_engine) != MA_SUCCESS)
    {
        LOG_ERROR(Engine, "AudioManager: failed to initialize miniaudio engine");
        return;
    }

    ma_sound_group_init(&g_engine, 0, nullptr, &g_sfxGroup);
    ma_sound_group_init(&g_engine, 0, nullptr, &g_musicGroup);
    g_initialized = true;

    // Listener direction and world-up for 2D (X right, Z forward = world Y).
    ma_engine_listener_set_direction(&g_engine, 0, 0.0f, 0.0f, -1.0f);
    ma_engine_listener_set_world_up(&g_engine, 0, 0.0f, 1.0f, 0.0f);

    ApplySettings();
    LOG_INFO(Engine, "AudioManager initialized");
}

void AudioManager::Shutdown()
{
    if (!g_initialized) return;

    if (g_musicLoaded)
    {
        ma_sound_stop(&g_musicSound);
        ma_sound_uninit(&g_musicSound);
        g_musicLoaded = false;
    }

    ma_sound_group_uninit(&g_sfxGroup);
    ma_sound_group_uninit(&g_musicGroup);
    ma_engine_uninit(&g_engine);
    g_initialized = false;

    LOG_INFO(Engine, "AudioManager shut down");
}

void AudioManager::PlayOneShot(std::string_view path, f32 volume)
{
    if (!g_initialized) return;

    // ma_engine_play_sound is fire-and-forget: miniaudio owns and frees the sound.
    if (ma_engine_play_sound(&g_engine, path.data(), &g_sfxGroup) != MA_SUCCESS)
        LOG_WARN(Engine, "AudioManager: PlayOneShot failed for '{}'", path);
}

void AudioManager::PlayMusic(std::string_view path, bool loop)
{
    if (!g_initialized) return;

    if (g_musicLoaded)
    {
        ma_sound_stop(&g_musicSound);
        ma_sound_uninit(&g_musicSound);
        g_musicLoaded = false;
    }

    // Stream the file and disable spatialization — music plays at flat volume.
    constexpr ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    const ma_result result = ma_sound_init_from_file(
        &g_engine, path.data(), flags, &g_musicGroup, nullptr, &g_musicSound);

    if (result != MA_SUCCESS)
    {
        LOG_WARN(Engine, "AudioManager: PlayMusic failed for '{}'", path);
        return;
    }

    ma_sound_set_looping(&g_musicSound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&g_musicSound);
    g_musicLoaded = true;
}

void AudioManager::StopMusic()
{
    if (!g_initialized || !g_musicLoaded) return;
    ma_sound_stop(&g_musicSound);
    ma_sound_uninit(&g_musicSound);
    g_musicLoaded = false;
}

void AudioManager::PauseMusic()
{
    if (g_initialized && g_musicLoaded)
        ma_sound_stop(&g_musicSound);
}

void AudioManager::ResumeMusic()
{
    if (g_initialized && g_musicLoaded)
        ma_sound_start(&g_musicSound);
}

void AudioManager::SetMasterVolume(f32 volume)
{
    g_masterVolume = volume;
    if (g_initialized) ApplyGroupVolumes();
}

void AudioManager::SetMusicVolume(f32 volume)
{
    g_musicVolume = volume;
    if (g_initialized) ApplyGroupVolumes();
}

void AudioManager::SetSfxVolume(f32 volume)
{
    g_sfxVolume = volume;
    if (g_initialized) ApplyGroupVolumes();
}

void AudioManager::ApplySettings()
{
    g_masterVolume = Settings::Audio.MasterVolume;
    g_musicVolume  = Settings::Audio.MusicVolume;
    g_sfxVolume    = Settings::Audio.SfxVolume;
    LOG_INFO(Engine, "AudioManager: Applying settings with master={}, music={}, sfx={}",
             g_masterVolume, g_musicVolume, g_sfxVolume);
    if (g_initialized) ApplyGroupVolumes();
}

void AudioManager::SetListenerPosition(Vec2 position)
{
    if (!g_initialized) return;
    // Map 2D world (X, Y) → audio (X, 0, Y) so that left/right and up/down
    // distances both contribute to attenuation in a top-down view.
    ma_engine_listener_set_position(&g_engine, 0, position.X, 0.0f, position.Y);
}

void* AudioManager::CreateSpatialSound(std::string_view path, bool loop, f32 volume)
{
    if (!g_initialized) return nullptr;

    auto* sound = new ma_sound{};

    // Fully decode for minimal latency on position updates.
    constexpr ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    const ma_result result = ma_sound_init_from_file(
        &g_engine, path.data(), flags, &g_sfxGroup, nullptr, sound);

    if (result != MA_SUCCESS)
    {
        LOG_WARN(Engine, "AudioManager: CreateSpatialSound failed for '{}'", path);
        delete sound;
        return nullptr;
    }

    ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(sound, volume);
    ma_sound_set_positioning(sound, ma_positioning_absolute);
    ma_sound_set_spatialization_enabled(sound, MA_TRUE);
    ma_sound_start(sound);
    return sound;
}

void AudioManager::SetSpatialSoundPosition(void* handle, Vec2 position, f32 radius)
{
    if (!handle) return;
    auto* sound = static_cast<ma_sound*>(handle);
    ma_sound_set_position(sound, position.X, 0.0f, position.Y);
    // Full volume within 10 % of the radius; silent beyond the radius.
    ma_sound_set_min_distance(sound, radius * 0.1f);
    ma_sound_set_max_distance(sound, radius);
}

void AudioManager::DestroySpatialSound(void* handle)
{
    if (!handle) return;
    auto* sound = static_cast<ma_sound*>(handle);
    ma_sound_stop(sound);
    ma_sound_uninit(sound);
    delete sound;
}

} // namespace Arcbit
