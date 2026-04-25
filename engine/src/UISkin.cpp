#include <arcbit/ui/UISkin.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <fstream>

namespace Arcbit
{
namespace
{
    Color ColorFromJson(const nlohmann::json& j, const Color fallback)
    {
        if (!j.is_array() || j.size() < 4) return fallback;
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    }

    Color GetColor(const nlohmann::json& obj, const char* key, const Color fallback)
    {
        if (!obj.contains(key)) return fallback;
        return ColorFromJson(obj[key], fallback);
    }

    nlohmann::json ColorToJson(const Color& c) { return {c.R, c.G, c.B, c.A}; }
} // anonymous namespace

UISkin UISkin::LoadFromFile(const std::string_view path)
{
    const std::string pathStr(path);
    std::ifstream     file(pathStr);
    if (!file.is_open()) {
        LOG_WARN(Engine, "UISkin: could not open '{}'", path);
        return Default();
    }

    nlohmann::json j;
    try { j = nlohmann::json::parse(file); }
    catch (const std::exception& e) {
        LOG_WARN(Engine, "UISkin: parse error in '{}': {}", path, e.what());
        return Default();
    }

    UISkin skin;
    skin.FontScale = j.value("fontScale", 1.0f);

    if (j.contains("panel")) {
        const auto& p    = j["panel"];
        skin.PanelBg     = GetColor(p, "background", skin.PanelBg);
        skin.PanelBorder = GetColor(p, "border", skin.PanelBorder);
    }
    if (j.contains("button")) {
        const auto& b       = j["button"];
        skin.ButtonNormal   = GetColor(b, "normal", skin.ButtonNormal);
        skin.ButtonHovered  = GetColor(b, "hovered", skin.ButtonHovered);
        skin.ButtonPressed  = GetColor(b, "pressed", skin.ButtonPressed);
        skin.ButtonDisabled = GetColor(b, "disabled", skin.ButtonDisabled);
    }
    if (j.contains("text")) {
        const auto& t     = j["text"];
        skin.TextNormal   = GetColor(t, "normal", skin.TextNormal);
        skin.TextDisabled = GetColor(t, "disabled", skin.TextDisabled);
        skin.TextLabel    = GetColor(t, "label", skin.TextLabel);
    }
    if (j.contains("progressBar")) {
        const auto& pb    = j["progressBar"];
        skin.ProgressBg   = GetColor(pb, "background", skin.ProgressBg);
        skin.ProgressFill = GetColor(pb, "fill", skin.ProgressFill);
    }
    if (j.contains("scrollBar")) {
        const auto& sb          = j["scrollBar"];
        skin.ScrollTrack        = GetColor(sb, "track", skin.ScrollTrack);
        skin.ScrollThumb        = GetColor(sb, "thumb", skin.ScrollThumb);
        skin.ScrollThumbHovered = GetColor(sb, "thumbHovered", skin.ScrollThumbHovered);
    }
    skin.AccentColor  = GetColor(j, "accentColor", skin.AccentColor);
    skin.OverlayColor = GetColor(j, "overlayColor", skin.OverlayColor);

    if (j.contains("sounds")) {
        const auto& s = j["sounds"];
        if (s.contains("focusMove"))  skin.SoundFocusMove  = s["focusMove"].get<std::string>();
        if (s.contains("activate"))   skin.SoundActivate   = s["activate"].get<std::string>();
        if (s.contains("back"))       skin.SoundBack        = s["back"].get<std::string>();
        if (s.contains("sliderTick")) skin.SoundSliderTick  = s["sliderTick"].get<std::string>();
        if (s.contains("toggle"))     skin.SoundToggle      = s["toggle"].get<std::string>();
    }

    LOG_DEBUG(Engine, "UISkin: loaded '{}'", path);
    return skin;
}

bool UISkin::SaveToFile(const std::string_view path) const
{
    nlohmann::json j;
    j["fontScale"] = FontScale;
    j["panel"]     = {{"background", ColorToJson(PanelBg)}, {"border", ColorToJson(PanelBorder)}};
    j["button"]    = {
        {"normal", ColorToJson(ButtonNormal)}, {"hovered", ColorToJson(ButtonHovered)},
        {"pressed", ColorToJson(ButtonPressed)}, {"disabled", ColorToJson(ButtonDisabled)}
    };
    j["text"] = {
        {"normal", ColorToJson(TextNormal)}, {"disabled", ColorToJson(TextDisabled)},
        {"label", ColorToJson(TextLabel)}
    };
    j["progressBar"] = {{"background", ColorToJson(ProgressBg)}, {"fill", ColorToJson(ProgressFill)}};
    j["scrollBar"]   = {
        {"track", ColorToJson(ScrollTrack)},
        {"thumb", ColorToJson(ScrollThumb)},
        {"thumbHovered", ColorToJson(ScrollThumbHovered)}
    };
    j["accentColor"] = ColorToJson(AccentColor);
    j["overlayColor"] = ColorToJson(OverlayColor);
    j["sounds"] = {
        {"focusMove",  SoundFocusMove},
        {"activate",   SoundActivate},
        {"back",       SoundBack},
        {"sliderTick", SoundSliderTick},
        {"toggle",     SoundToggle}
    };

    const std::string pathStr(path);
    std::ofstream     file(pathStr);
    if (!file.is_open()) {
        LOG_WARN(Engine, "UISkin: could not write '{}'", path);
        return false;
    }
    file << j.dump(2);
    return true;
}
} // namespace Arcbit
