#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/render/Font.h>

#include <string_view>

namespace Arcbit {

// Shared visual theme passed to every widget during collect.
// All colors are RGBA (0-1 range). Load from JSON with LoadFromFile().
struct UISkin
{
    const FontAtlas* Font = nullptr; // must be set after loading; not serialized
    f32 FontScale = 1.0f;

    // Panel
    Color PanelBg          = { 0.10f, 0.10f, 0.12f, 0.90f };
    Color PanelBorder      = { 0.30f, 0.30f, 0.35f, 1.00f };

    // Button states
    Color ButtonNormal     = { 0.20f, 0.20f, 0.25f, 1.00f };
    Color ButtonHovered    = { 0.30f, 0.30f, 0.38f, 1.00f };
    Color ButtonPressed    = { 0.14f, 0.14f, 0.18f, 1.00f };
    Color ButtonDisabled   = { 0.15f, 0.15f, 0.15f, 0.60f };

    // Text
    Color TextNormal       = { 1.00f, 1.00f, 1.00f, 1.00f };
    Color TextDisabled     = { 0.50f, 0.50f, 0.50f, 1.00f };
    Color TextLabel        = { 0.85f, 0.85f, 0.85f, 1.00f };

    // Progress bar
    Color ProgressBg       = { 0.15f, 0.15f, 0.15f, 1.00f };
    Color ProgressFill     = { 0.20f, 0.65f, 0.25f, 1.00f };

    // Scroll panel
    Color ScrollTrack        = { 0.08f, 0.08f, 0.10f, 1.00f };
    Color ScrollThumb        = { 0.30f, 0.30f, 0.38f, 1.00f };
    Color ScrollThumbHovered = { 0.45f, 0.45f, 0.55f, 1.00f };

    // Accent — used for focused/listening states (e.g. rebind screen row)
    Color AccentColor      = { 0.38f, 0.60f, 0.90f, 1.00f };
    
    Color OverlayColor     = { 0.10f, 0.10f, 0.12f, 0.20f };

    // Per-screen layer base set by UIManager based on stack index.
    // Ensures sprites from screens higher in the stack always sort above lower ones.
    i32 ScreenLayerBase = 0;

    // Load from a JSON file. Returns Default() if the file cannot be opened or parsed.
    // Does not set Font — assign that separately after loading.
    [[nodiscard]] static UISkin LoadFromFile(std::string_view path);

    // Returns a skin with the default values above.
    [[nodiscard]] static UISkin Default() { return {}; }

    // Write to JSON. Returns false on failure.
    bool SaveToFile(std::string_view path) const;
};

} // namespace Arcbit
