#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <arcbit/assets/TextureManager.h>
#include <arcbit/render/RenderDevice.h>
#include <arcbit/render/RenderTypes.h>
#include <arcbit/core/Log.h>

#include <filesystem>

namespace Arcbit {

TextureManager::TextureManager(RenderDevice& device)
    : m_Device(device)
{}

TextureManager::~TextureManager()
{
    // Clear() requires a live device — Application calls it explicitly in the
    // shutdown sequence before DestroyDevice, so this should already be empty.
    Clear();
}

// ---------------------------------------------------------------------------
// Load / Unload
// ---------------------------------------------------------------------------

TextureHandle TextureManager::Load(std::string_view path)
{
    // Normalize separators and remove redundant components so that
    // "assets/textures/../textures/foo.png" and "assets/textures/foo.png"
    // both map to the same cache entry.
    const std::string key = std::filesystem::path(path).lexically_normal().string();

    // Cache hit — return existing handle without re-uploading.
    if (const auto it = m_PathCache.find(key); it != m_PathCache.end())
        return it->second.Handle;

    return LoadFromDisk(key);
}

void TextureManager::Reload(TextureHandle handle)
{
    const auto it = m_HandleToPath.find(handle.Value);
    if (it == m_HandleToPath.end())
        return;

    ReloadFromDisk(it->second);
}

void TextureManager::Unload(TextureHandle handle)
{
    const auto handleIt = m_HandleToPath.find(handle.Value);
    if (handleIt == m_HandleToPath.end())
        return;

    const std::string path = handleIt->second;
    m_HandleToPath.erase(handleIt);

    if (const auto pathIt = m_PathCache.find(path); pathIt != m_PathCache.end()) {
        m_Device.DestroyTexture(pathIt->second.Handle);
        m_PathCache.erase(pathIt);
    }
}

void TextureManager::Clear()
{
    for (const auto& [path, entry] : m_PathCache)
        m_Device.DestroyTexture(entry.Handle);

    m_PathCache.clear();
    m_HandleToPath.clear();
}

// ---------------------------------------------------------------------------
// Hot-reload
// ---------------------------------------------------------------------------

void TextureManager::CheckReloads()
{
    for (auto& [path, entry] : m_PathCache) {
        std::error_code ec;
        const auto currentTime = std::filesystem::last_write_time(path, ec);
        if (ec) continue; // file deleted or inaccessible — skip silently

        if (currentTime != entry.LastModified)
            ReloadFromDisk(path);
    }
}

// ---------------------------------------------------------------------------
// Info query
// ---------------------------------------------------------------------------

TextureInfo TextureManager::GetInfo(TextureHandle handle) const
{
    const auto it = m_HandleToPath.find(handle.Value);
    if (it == m_HandleToPath.end())
        return {};

    const auto entryIt = m_PathCache.find(it->second);
    if (entryIt == m_PathCache.end())
        return {};

    return TextureInfo{ entryIt->second.Width, entryIt->second.Height };
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

TextureHandle TextureManager::LoadFromDisk(const std::string& path)
{
    int w, h, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_ERROR(Engine, "TextureManager: failed to load '{}' — {}", path, stbi_failure_reason());
        return TextureHandle::Invalid();
    }

    TextureDesc desc{};
    desc.Width     = static_cast<u32>(w);
    desc.Height    = static_cast<u32>(h);
    desc.Format    = Format::RGBA8_UNorm;
    desc.Usage     = TextureUsage::Sampled | TextureUsage::Transfer;
    desc.DebugName = path.c_str();

    const TextureHandle handle = m_Device.CreateTexture(desc);
    if (!handle.IsValid()) {
        stbi_image_free(pixels);
        LOG_ERROR(Engine, "TextureManager: GPU texture creation failed for '{}'", path);
        return TextureHandle::Invalid();
    }

    m_Device.UploadTexture(handle, pixels, static_cast<u64>(w) * h * 4);
    stbi_image_free(pixels);

    CacheEntry entry{};
    entry.Handle       = handle;
    entry.Path         = path;
    entry.Width        = static_cast<u32>(w);
    entry.Height       = static_cast<u32>(h);
    entry.LastModified = std::filesystem::last_write_time(path);

    m_PathCache.emplace(path, entry);
    m_HandleToPath.emplace(handle.Value, path);

    LOG_DEBUG(Engine, "TextureManager: loaded '{}' ({}x{})", path, w, h);
    return handle;
}

void TextureManager::ReloadFromDisk(const std::string& path)
{
    auto it = m_PathCache.find(path);
    if (it == m_PathCache.end()) return;

    CacheEntry& entry = it->second;

    int w, h, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_WARN(Engine, "TextureManager: hot-reload failed for '{}' — {}", path, stbi_failure_reason());
        return;
    }

    if (static_cast<u32>(w) == entry.Width && static_cast<u32>(h) == entry.Height) {
        // Same dimensions — re-upload to the existing handle so all users
        // automatically see the new content without handle invalidation.
        m_Device.UploadTexture(entry.Handle, pixels, static_cast<u64>(w) * h * 4);
        entry.LastModified = std::filesystem::last_write_time(path);
        LOG_INFO(Engine, "TextureManager: hot-reloaded '{}'", path);
    } else {
        // Dimension change would invalidate the handle and break existing users.
        // Log a warning and leave the old texture in place.
        LOG_WARN(Engine, "TextureManager: skipping hot-reload for '{}' — "
                 "dimensions changed ({}x{} → {}x{})",
                 path, entry.Width, entry.Height, w, h);
    }

    stbi_image_free(pixels);
}

} // namespace Arcbit
