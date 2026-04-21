#include <arcbit/ui/UILoader.h>
#include <arcbit/ui/UIScreen.h>
#include <arcbit/ui/Widgets.h>
#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <unordered_map>

namespace Arcbit
{

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Module-scope type registry
// ---------------------------------------------------------------------------

namespace {

struct TypeEntry
{
    UILoader::Factory                        factory;
    std::function<void(UIWidget&, const json&)> apply; // may be null for custom types
};

std::unordered_map<std::string, TypeEntry>& Registry()
{
    static std::unordered_map<std::string, TypeEntry> s_map;
    return s_map;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void UILoader::RegisterType(const std::string_view typeName, Factory factory)
{
    Registry()[std::string(typeName)] = { std::move(factory), nullptr };
}

// ---------------------------------------------------------------------------
// JSON read helpers
// ---------------------------------------------------------------------------

static Vec2 ReadVec2(const json& j, const Vec2 def = {})
{
    if (!j.is_array() || j.size() < 2) return def;
    return { j[0].get<f32>(), j[1].get<f32>() };
}

static Color ReadColor(const json& j, const Color def = {})
{
    if (!j.is_array() || j.size() < 4) return def;
    return { j[0].get<f32>(), j[1].get<f32>(), j[2].get<f32>(), j[3].get<f32>() };
}

// ---------------------------------------------------------------------------
// Property appliers — one per widget type
// ---------------------------------------------------------------------------

static void ApplyBase(UIWidget& w, const json& j)
{
    if (auto it = j.find("name");         it != j.end()) w.Name        = it->get<std::string>();
    if (auto it = j.find("size");         it != j.end()) w.Size        = ReadVec2(*it, w.Size);
    if (auto it = j.find("size_percent"); it != j.end()) w.SizePercent = ReadVec2(*it);
    if (auto it = j.find("anchor");       it != j.end()) w.Anchor      = ReadVec2(*it);
    if (auto it = j.find("pivot");        it != j.end()) w.Pivot       = ReadVec2(*it);
    if (auto it = j.find("offset");       it != j.end()) w.Offset      = ReadVec2(*it);
    if (auto it = j.find("zorder");       it != j.end()) w.ZOrder      = it->get<i32>();
    if (auto it = j.find("opacity");      it != j.end()) w.Opacity     = it->get<f32>();
    if (auto it = j.find("visible");      it != j.end()) w.Visible     = it->get<bool>();
    if (auto it = j.find("enabled");      it != j.end()) w.Enabled     = it->get<bool>();
    if (auto it = j.find("focusable");    it != j.end()) w.Focusable   = it->get<bool>();
}

static void ApplyPanel(UIWidget& w, const json& j)
{
    auto& p = static_cast<Panel&>(w);
    if (auto it = j.find("background_color"); it != j.end()) p.BackgroundColor = ReadColor(*it);
    if (auto it = j.find("draw_border");      it != j.end()) p.DrawBorder      = it->get<bool>();
}

static void ApplyLabel(UIWidget& w, const json& j)
{
    auto& l = static_cast<Label&>(w);
    if (auto it = j.find("text");        it != j.end()) l.Text       = it->get<std::string>();
    if (auto it = j.find("text_key");    it != j.end()) l.Text       = Loc::Get(it->get<std::string>());
    if (auto it = j.find("word_wrap");   it != j.end()) l.WordWrap   = it->get<bool>();
    if (auto it = j.find("auto_center"); it != j.end()) l.AutoCenter = it->get<bool>();
    if (auto it = j.find("text_color");  it != j.end()) l.TextColor  = ReadColor(*it);
    if (auto it = j.find("align");       it != j.end()) {
        const std::string s = it->get<std::string>();
        if      (s == "center") l.Align = TextAlign::Center;
        else if (s == "right")  l.Align = TextAlign::Right;
        else                    l.Align = TextAlign::Left;
    }
}

static void ApplyButton(UIWidget& w, const json& j)
{
    auto& b = static_cast<Button&>(w);
    if (auto it = j.find("text");       it != j.end()) b.Text      = it->get<std::string>();
    if (auto it = j.find("text_key");   it != j.end()) b.Text      = Loc::Get(it->get<std::string>());
    if (auto it = j.find("text_color"); it != j.end()) b.TextColor = ReadColor(*it);
}

static void ApplyProgressBar(UIWidget& w, const json& j)
{
    auto& p = static_cast<ProgressBar&>(w);
    if (auto it = j.find("value");      it != j.end()) p.Value     = it->get<f32>();
    if (auto it = j.find("fill_color"); it != j.end()) p.FillColor = ReadColor(*it);
}

static void ApplyScrollPanel(UIWidget& w, const json& j)
{
    auto& s = static_cast<ScrollPanel&>(w);
    if (auto it = j.find("content_height");  it != j.end()) s.ContentHeight  = it->get<f32>();
    if (auto it = j.find("scrollbar_width"); it != j.end()) s.ScrollbarWidth = it->get<f32>();
}

static void ApplyNineSlice(UIWidget& w, const json& j)
{
    auto& n = static_cast<NineSlice&>(w);
    if (auto it = j.find("tint");             it != j.end()) n.Tint           = ReadColor(*it);
    if (auto it = j.find("uv_border_left");   it != j.end()) n.UVBorderLeft   = it->get<f32>();
    if (auto it = j.find("uv_border_right");  it != j.end()) n.UVBorderRight  = it->get<f32>();
    if (auto it = j.find("uv_border_top");    it != j.end()) n.UVBorderTop    = it->get<f32>();
    if (auto it = j.find("uv_border_bottom"); it != j.end()) n.UVBorderBottom = it->get<f32>();
    if (auto it = j.find("pixel_left");       it != j.end()) n.PixelLeft      = it->get<f32>();
    if (auto it = j.find("pixel_right");      it != j.end()) n.PixelRight     = it->get<f32>();
    if (auto it = j.find("pixel_top");        it != j.end()) n.PixelTop       = it->get<f32>();
    if (auto it = j.find("pixel_bottom");     it != j.end()) n.PixelBottom    = it->get<f32>();
}

static void ApplyImage(UIWidget& w, const json& j)
{
    auto& img = static_cast<Image&>(w);
    if (auto it = j.find("tint"); it != j.end()) img.Tint = ReadColor(*it);
    if (auto it = j.find("uv");   it != j.end() && it->is_array() && it->size() >= 4)
        img.UV = { (*it)[0].get<f32>(), (*it)[1].get<f32>(), (*it)[2].get<f32>(), (*it)[3].get<f32>() };
}

// ---------------------------------------------------------------------------
// EnsureBuiltins — register all engine widget types once
// ---------------------------------------------------------------------------

void UILoader::EnsureBuiltins()
{
    auto& r = Registry();
    if (!r.empty()) return; // already registered

    r["Panel"]       = { [] { return std::make_unique<Panel>();       }, ApplyPanel       };
    r["Overlay"]     = { [] { return std::make_unique<Overlay>();     }, ApplyPanel       }; // inherits Panel
    r["Label"]       = { [] { return std::make_unique<Label>();       }, ApplyLabel       };
    r["Button"]      = { [] { return std::make_unique<Button>();      }, ApplyButton      };
    r["Image"]       = { [] { return std::make_unique<Image>();       }, ApplyImage       };
    r["NineSlice"]   = { [] { return std::make_unique<NineSlice>();   }, ApplyNineSlice   };
    r["ProgressBar"] = { [] { return std::make_unique<ProgressBar>(); }, ApplyProgressBar };
    r["ScrollPanel"] = { [] { return std::make_unique<ScrollPanel>(); }, ApplyScrollPanel };
}

// ---------------------------------------------------------------------------
// Recursive widget builder
// ---------------------------------------------------------------------------

static std::unique_ptr<UIWidget> BuildWidget(const json& j)
{
    const auto typeIt = j.find("type");
    if (typeIt == j.end()) { LOG_WARN(UI, "arcui widget missing 'type' field"); return nullptr; }

    const std::string type = typeIt->get<std::string>();
    auto& reg = Registry();
    const auto entryIt = reg.find(type);
    if (entryIt == reg.end()) { LOG_WARN(UI, "arcui unknown widget type: {}", type); return nullptr; }

    auto widget = entryIt->second.factory();
    ApplyBase(*widget, j);
    if (entryIt->second.apply) entryIt->second.apply(*widget, j);

    if (const auto childIt = j.find("children"); childIt != j.end() && childIt->is_array())
        for (const auto& childJson : *childIt)
            if (auto child = BuildWidget(childJson))
                widget->AddChildRaw(std::move(child));

    return widget;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

bool UILoader::Load(const std::string_view path, UIScreen& screen)
{
    EnsureBuiltins();

    std::ifstream file{std::string(path)};
    if (!file.is_open()) {
        LOG_ERROR(UI, "arcui: cannot open '{}'", path);
        return false;
    }

    json root;
    try { root = json::parse(file); }
    catch (const json::exception& e) {
        LOG_ERROR(UI, "arcui: parse error in '{}': {}", path, e.what());
        return false;
    }

    // Build widget tree
    if (const auto widgetsIt = root.find("widgets"); widgetsIt != root.end() && widgetsIt->is_array())
        for (const auto& wJson : *widgetsIt)
            if (auto w = BuildWidget(wJson))
                screen.AddRaw(std::move(w));

    // Populate meta section — numbers become f32, strings become str
    if (const auto metaIt = root.find("meta"); metaIt != root.end() && metaIt->is_object()) {
        for (const auto& [key, val] : metaIt->items()) {
            if (val.is_number()) screen.SetMetaF32(key, val.get<f32>());
            else if (val.is_string()) screen.SetMetaStr(key, val.get<std::string>());
        }
    }

    return true;
}

} // namespace Arcbit
