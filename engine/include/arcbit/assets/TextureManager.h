#pragma once

#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Arcbit {

class RenderDevice;

// ---------------------------------------------------------------------------
// TextureInfo
//
// Metadata returned by TextureManager::GetInfo().
// ---------------------------------------------------------------------------
struct TextureInfo
{
    u32 Width  = 0;
    u32 Height = 0;
};

// ---------------------------------------------------------------------------
// TextureManager
//
// Loads 2D textures from disk (PNG, JPG, BMP, TGA via stb_image) and
// caches them by canonical file path so duplicate loads are free.
//
// Typical usage
// -------------
//   // OnStart — load once, reuse the handle everywhere:
//   m_WoodsTex = GetTextures().Load("assets/textures/woods.png");
//
//   // OnShutdown — not required; Application calls Clear() automatically.
//   // To release one texture explicitly:
//   GetTextures().Unload(m_WoodsTex);
//
// Hot-reload
// ----------
//   CheckReloads() polls every loaded texture's file modification time.
//   If a file has changed, its pixels are re-uploaded to the same
//   TextureHandle — all existing users see the update automatically.
//   Application calls this once per second from the game loop.
//   Only works when texture dimensions are unchanged (same size on disk).
//
// Thread safety
// -------------
//   NOT thread-safe. Call from the main/game thread only.
// ---------------------------------------------------------------------------
class TextureManager
{
public:
    explicit TextureManager(RenderDevice& device);
    ~TextureManager();

    TextureManager(const TextureManager&)            = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // Load a texture from disk and cache it, or return the cached handle if
    // already loaded. The path is normalized so equivalent paths hit the cache.
    // Returns TextureHandle::Invalid() if the file cannot be opened or decoded.
    [[nodiscard]] TextureHandle Load(std::string_view path);

    // Force a reload of a previously loaded texture from its original path.
    // The handle remains valid — only the GPU contents are updated.
    // No-op if the handle is not in the cache or dimensions have changed.
    void Reload(TextureHandle handle);

    // Release a texture and remove it from the cache.
    // The handle becomes invalid after this call.
    void Unload(TextureHandle handle);

    // Release all cached textures and clear the cache.
    // Called automatically by Application during shutdown.
    void Clear();

    // Poll all loaded textures for file changes and re-upload modified ones.
    // Application calls this once per second. Can also be called manually.
    void CheckReloads();

    // Returns width/height of a cached texture, or zeroes if not found.
    [[nodiscard]] TextureInfo GetInfo(TextureHandle handle) const;

private:
    struct CacheEntry
    {
        TextureHandle                     Handle;
        std::string                       Path;
        u32                               Width        = 0;
        u32                               Height       = 0;
        std::filesystem::file_time_type   LastModified = {};
    };

    // Decode the file at the given (already-normalized) path, upload to GPU,
    // insert into both caches, and return the new handle.
    [[nodiscard]] TextureHandle LoadFromDisk(const std::string& path);

    // Re-decode and re-upload a cached entry (same handle, new pixels).
    void ReloadFromDisk(const std::string& path);

    RenderDevice& _device;

    // Primary lookup: normalized path → cache entry.
    std::unordered_map<std::string, CacheEntry> _pathCache;

    // Reverse lookup: handle.Value → path (for Unload and Reload).
    std::unordered_map<u32, std::string> _handleToPath;
};

} // namespace Arcbit
