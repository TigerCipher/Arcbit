#pragma once

#include <string>
#include <string_view>

namespace Arcbit
{
// ---------------------------------------------------------------------------
// Loc — static localization string table.
//
// Load one or more locale files in order; later files override earlier keys.
// Engine loads its own locale before OnStart; games call Load() in OnStart
// to add their own strings or override engine strings.
//
// Get() returns the localized string for a key, or the key itself if not found
// — so .arcui files remain human-readable even without a locale file loaded.
//
// Locale files are flat JSON objects: { "key": "translated string", ... }
// By convention, engine keys use the prefix "ui." (e.g. "ui.pause.title").
// ---------------------------------------------------------------------------
class Loc
{
public:
    // Load a locale JSON file, merging its entries into the current table.
    // Returns false and logs a warning if the file cannot be opened or parsed.
    [[nodiscard]] static bool Load(std::string_view path);

    // Return the localized string for key, or key itself if not found.
    [[nodiscard]] static const std::string& Get(std::string_view key);

    // Remove all loaded strings (useful for locale switching).
    static void Clear();

private:
    // Intentionally not exposed — all access goes through Get/Load/Clear.
};

} // namespace Arcbit
