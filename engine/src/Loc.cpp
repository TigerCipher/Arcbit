#include <arcbit/core/Loc.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_map>

namespace Arcbit
{

namespace {
    std::unordered_map<std::string, std::string>& Table()
    {
        static std::unordered_map<std::string, std::string> s_table;
        return s_table;
    }
}

bool Loc::Load(const std::string_view path)
{
    std::ifstream file{std::string(path)};
    if (!file.is_open()) {
        LOG_WARN(Engine, "Loc: cannot open '{}'", path);
        return false;
    }

    nlohmann::json root;
    try { root = nlohmann::json::parse(file); }
    catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Engine, "Loc: parse error in '{}': {}", path, e.what());
        return false;
    }

    auto& t = Table();
    for (const auto& [key, val] : root.items())
        if (val.is_string()) t[key] = val.get<std::string>();

    LOG_INFO(Engine, "Loc: loaded {} strings from '{}'", root.size(), path);
    return true;
}

const std::string& Loc::Get(const std::string_view key)
{
    auto& t = Table();
    const auto it = t.find(std::string(key));
    if (it != t.end()) return it->second;
    // Cache the key as its own value so we return a stable reference.
    return t.emplace(std::string(key), std::string(key)).first->second;
}

void Loc::Clear()
{
    Table().clear();
}

} // namespace Arcbit
