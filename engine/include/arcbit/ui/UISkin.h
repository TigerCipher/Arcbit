#pragma once

#include <arcbit/core/Math.h>
#include <arcbit/render/Font.h>

namespace Arcbit {

// Shared visual theme passed to every widget during collect.
// All colors are RGBA (0-1 range).
struct UISkin
{
    const FontAtlas* Font = nullptr;  // must be set before any widget collects

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

    // Font rendering scale (1.0 = baked pixel size).
    f32 FontScale          = 1.0f;
};

} // namespace Arcbit
