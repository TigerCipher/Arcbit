#include <arcbit/tilemap/TileMap.h>
#include <arcbit/assets/TextureManager.h>
#include <arcbit/core/Log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace Arcbit
{
void TileMap::RegisterAtlas(const u32              baseId, TileAtlas                atlas, const SamplerHandle sampler,
                            const std::string_view jsonPath, const std::string_view samplerName)
{
    // Keep _atlases sorted by BaseId for binary search in FindAtlas.
    auto it = std::lower_bound(_atlases.begin(), _atlases.end(), baseId,
                               [](const TileAtlasEntry& e, u32 id) { return e.BaseId < id; });

    // Compute the inclusive ID range this atlas owns (up to the next atlas BaseId - 1).
    const u32 tileCount = atlas.Columns() * atlas.Rows();
    const u32 endId     = baseId + tileCount - 1;
    LOG_INFO(Engine, "TileMap: registered atlas base={} end={} ({} tiles, {}x{})",
             baseId, endId, tileCount, atlas.Columns(), atlas.Rows());

    _atlases.insert(it, TileAtlasEntry{
                        baseId, std::move(atlas), sampler,
                        std::string(jsonPath), std::string(samplerName),
                    });
}

void TileMap::RegisterTile(const u32 tileId, TileDef def) { _tileDefs[tileId] = std::move(def); }

void TileMap::SetTile(const i32 tileX, const i32 tileY, const u32 layerIndex, const u32 tileId)
{
    const i32 chunkX    = static_cast<i32>(std::floor(static_cast<f32>(tileX) / ChunkSize));
    const i32 chunkY    = static_cast<i32>(std::floor(static_cast<f32>(tileY) / ChunkSize));
    const u32 localX    = static_cast<u32>(tileX - chunkX * static_cast<i32>(ChunkSize));
    const u32 localY    = static_cast<u32>(tileY - chunkY * static_cast<i32>(ChunkSize));
    const u32 cellIndex = localY * ChunkSize + localX;

    auto& [Tiles]                = GetOrCreateChunk(chunkX, chunkY);
    Tiles[layerIndex][cellIndex] = tileId;

    // Invalidate the greedy collider mesh for this chunk; next query rebuilds.
    _chunkColliders.erase(ChunkKey(chunkX, chunkY));
}

u32 TileMap::GetTile(const i32 tileX, const i32 tileY, const u32 layerIndex) const
{
    const i32 chunkX    = static_cast<i32>(std::floor(static_cast<f32>(tileX) / ChunkSize));
    const i32 chunkY    = static_cast<i32>(std::floor(static_cast<f32>(tileY) / ChunkSize));
    const u32 localX    = static_cast<u32>(tileX - chunkX * static_cast<i32>(ChunkSize));
    const u32 localY    = static_cast<u32>(tileY - chunkY * static_cast<i32>(ChunkSize));
    const u32 cellIndex = localY * ChunkSize + localX;

    const TileChunk* chunk = GetChunk(chunkX, chunkY);
    return chunk ? chunk->Tiles[layerIndex][cellIndex] : 0u;
}

Vec2 TileMap::TileToWorld(const i32 tileX, const i32 tileY) const
{
    return {static_cast<f32>(tileX) * _tileSize, static_cast<f32>(tileY) * _tileSize};
}

void TileMap::WorldToTile(const Vec2 worldPos, i32& outX, i32& outY) const
{
    outX = static_cast<i32>(std::floor(worldPos.X / _tileSize));
    outY = static_cast<i32>(std::floor(worldPos.Y / _tileSize));
}

bool TileMap::IsSolid(const i32 tileX, const i32 tileY) const
{
    for (u32 layer = 0; layer < LayerCount; ++layer) {
        const u32 id = GetTile(tileX, tileY, layer);
        if (id == 0) continue;
        const TileDef* def = FindTileDef(id);
        if (def && def->Solid) return true;
    }
    return false;
}

bool TileMap::BlocksLight(const i32 tileX, const i32 tileY) const
{
    for (u32 layer = 0; layer < LayerCount; ++layer) {
        const u32 id = GetTile(tileX, tileY, layer);
        if (id == 0) continue;
        const TileDef* def = FindTileDef(id);
        if (def && def->BlocksLight) return true;
    }
    return false;
}

UVRect TileMap::GetTileUV(const u32 tileId) const
{
    const TileAtlasEntry* entry = FindAtlas(tileId);
    if (!entry) return {};
    const u32 localId = tileId - entry->BaseId;
    return entry->Atlas.GetUV(localId % entry->Atlas.Columns(), localId / entry->Atlas.Columns());
}

const std::unordered_map<u64, TileChunk>& TileMap::GetChunks() const { return _chunks; }

const TileAtlasEntry* TileMap::FindAtlas(const u32 tileId) const
{
    if (_atlases.empty() || tileId == 0) return nullptr;

    // Find the last atlas whose BaseId <= tileId.
    auto it = std::upper_bound(_atlases.begin(), _atlases.end(), tileId,
                               [](u32 id, const TileAtlasEntry& e) { return id < e.BaseId; });
    if (it == _atlases.begin()) return nullptr;
    --it;
    return &(*it);
}

const TileDef* TileMap::FindTileDef(const u32 tileId) const
{
    const auto it = _tileDefs.find(tileId);
    return it != _tileDefs.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Tile collision — greedy meshing per chunk
// ---------------------------------------------------------------------------

const TileDef* TileMap::GetCollisionDef(const i32 tileX, const i32 tileY) const
{
    // First solid TileDef across layers wins (Ground → Objects → Overlay order).
    // Documented limitation: a cell with conflicting solid TileDefs across layers
    // collapses to a single collider class. Fine for 22A; multi-layer trigger
    // composition is a 22F concern.
    for (u32 layer = 0; layer < LayerCount; ++layer) {
        const u32 id = GetTile(tileX, tileY, layer);
        if (id == 0) continue;
        const TileDef* def = FindTileDef(id);
        if (def && def->Solid) return def;
    }
    return nullptr;
}

void TileMap::BuildChunkColliders(const i32                      chunkX, const i32 chunkY,
                                  std::vector<TileColliderRect>& out) const
{
    out.clear();
    const TileChunk* chunk = GetChunk(chunkX, chunkY);
    if (!chunk) return;

    // Cache the collision def per cell in the chunk; lookups are otherwise
    // repeated O(layers) per cell during the merge scan.
    const TileDef* defs[ChunkSize * ChunkSize] = {};
    bool           anySolid                    = false;
    const i32      baseTileX                   = chunkX * static_cast<i32>(ChunkSize);
    const i32      baseTileY                   = chunkY * static_cast<i32>(ChunkSize);
    for (u32 y = 0; y < ChunkSize; ++y) {
        for (u32 x = 0; x < ChunkSize; ++x) {
            const TileDef* def = GetCollisionDef(baseTileX + static_cast<i32>(x),
                                                 baseTileY + static_cast<i32>(y));
            defs[y * ChunkSize + x] = def;
            if (def) anySolid = true;
        }
    }
    if (!anySolid) return;

    // Two TileDefs merge into the same rect iff they describe the same
    // collision class. Pointer identity is the fast path; if pointers differ,
    // deep-compare only the collision-relevant fields.
    auto sameClass = [](const TileDef* a, const TileDef* b) noexcept {
        if (a == b) return true;
        if (!a || !b) return false;
        return a->Layer == b->Layer && a->BlockedFrom == b->BlockedFrom;
    };

    bool visited[ChunkSize * ChunkSize] = {};

    for (u32 y = 0; y < ChunkSize; ++y) {
        for (u32 x = 0; x < ChunkSize; ++x) {
            const u32      idx = y * ChunkSize + x;
            const TileDef* def = defs[idx];
            if (visited[idx] || !def) continue;

            // Expand right while still in the same collision class and not visited.
            u32 w = 1;
            while (x + w < ChunkSize) {
                const u32 ridx = y * ChunkSize + (x + w);
                if (visited[ridx] || !sameClass(defs[ridx], def)) break;
                ++w;
            }

            // Expand down — every cell in [x, x+w) on row (y+h) must match.
            u32 h = 1;
            while (y + h < ChunkSize) {
                bool rowMatches = true;
                for (u32 dx = 0; dx < w; ++dx) {
                    const u32 ridx = (y + h) * ChunkSize + (x + dx);
                    if (visited[ridx] || !sameClass(defs[ridx], def)) {
                        rowMatches = false;
                        break;
                    }
                }
                if (!rowMatches) break;
                ++h;
            }

            // Mark the rectangle as visited and emit a TileColliderRect.
            for (u32 dy = 0; dy < h; ++dy)
                for (u32 dx = 0; dx < w; ++dx)
                    visited[(y + dy) * ChunkSize + (x + dx)] = true;

            const f32 worldX0 = static_cast<f32>(baseTileX + static_cast<i32>(x)) * _tileSize;
            const f32 worldY0 = static_cast<f32>(baseTileY + static_cast<i32>(y)) * _tileSize;
            const f32 worldX1 = worldX0 + static_cast<f32>(w) * _tileSize;
            const f32 worldY1 = worldY0 + static_cast<f32>(h) * _tileSize;

            out.push_back(TileColliderRect{
                .BlockedFrom = def->BlockedFrom,
                .WorldAABB   = AABB::FromMinMax({worldX0, worldY0}, {worldX1, worldY1}),
                .Layer       = def->Layer,
            });
        }
    }
}

const std::vector<TileColliderRect>& TileMap::GetChunkColliders(const i32 chunkX,
                                                                const i32 chunkY) const
{
    const u64 key = ChunkKey(chunkX, chunkY);
    if (const auto it = _chunkColliders.find(key); it != _chunkColliders.end())
        return it->second;

    auto& list = _chunkColliders[key];
    BuildChunkColliders(chunkX, chunkY, list);
    return list;
}

void TileMap::LogStats() const
{
    u32 tileCounts[LayerCount] = {};
    for (const auto& [Tiles] : _chunks | std::views::values) {
        for (u32 layer = 0; layer < LayerCount; ++layer)
            for (u32 cell = 0; cell < ChunkSize * ChunkSize; ++cell)
                if (Tiles[layer][cell] != 0)
                    ++tileCounts[layer];
    }

    LOG_INFO(Engine, "TileMap stats: {} chunks | tileSize={:.0f}",
             _chunks.size(), _tileSize);
    LOG_INFO(Engine, "  Layer 0 (Ground):  {} tiles set", tileCounts[0]);
    LOG_INFO(Engine, "  Layer 1 (Objects): {} tiles set", tileCounts[1]);
    LOG_INFO(Engine, "  Layer 2 (Overlay): {} tiles set", tileCounts[2]);
    LOG_INFO(Engine, "  Atlases: {} registered | TileDefs: {} registered",
             _atlases.size(), _tileDefs.size());

    for (const auto& entry : _atlases) {
        const u32 tileCount = entry.Atlas.Columns() * entry.Atlas.Rows();
        LOG_INFO(Engine, "  Atlas baseId={} - {}x{} grid - IDs {}-{}",
                 entry.BaseId, entry.Atlas.Columns(), entry.Atlas.Rows(),
                 entry.BaseId, entry.BaseId + tileCount - 1);
    }
}

bool TileMap::LoadMap(const std::string_view path, TextureManager&               textures,
                      const SamplerHandle    nearestSampler, const SamplerHandle linearSampler)
{
    std::ifstream f{std::string(path)};
    if (!f) {
        LOG_WARN(Engine, "TileMap: cannot open map file '{}'", path);
        return false;
    }

    nlohmann::json j;
    try { f >> j; }
    catch (const std::exception& e) {
        LOG_WARN(Engine, "TileMap: JSON parse error in '{}': {}", path, e.what());
        return false;
    }

    _tileSize = j.value("tile_size", _tileSize);

    for (const auto& a : j.value("atlases", nlohmann::json::array())) {
        const u32           baseId      = a.at("base_id").get<u32>();
        const std::string   atlasPath   = a.at("path").get<std::string>();
        const std::string   samplerName = a.value("sampler", "nearest");
        const SamplerHandle sampler     = (samplerName == "linear") ? linearSampler : nearestSampler;

        if (!LoadAtlasJson(baseId, atlasPath, sampler, samplerName, textures))
            return false;
    }

    for (const auto& c : j.value("chunks", nlohmann::json::array())) {
        const i32  cx    = c.at("x").get<i32>();
        const i32  cy    = c.at("y").get<i32>();
        TileChunk& chunk = GetOrCreateChunk(cx, cy);

        auto fillLayer = [](TileChunk& ch, const u32 layer, const nlohmann::json& arr) {
            u32 i = 0;
            for (const auto& v : arr)
                ch.Tiles[layer][i++] = v.get<u32>();
        };

        fillLayer(chunk, 0, c.value("ground", nlohmann::json::array()));
        fillLayer(chunk, 1, c.value("objects", nlohmann::json::array()));
        fillLayer(chunk, 2, c.value("overlay", nlohmann::json::array()));
    }

    LOG_INFO(Engine, "TileMap: loaded '{}' - {} atlases, {} chunks",
             path, _atlases.size(), _chunks.size());
    return true;
}

bool TileMap::SaveMap(const std::string_view path) const
{
    nlohmann::json j;
    j["version"]   = "1.0";
    j["tile_size"] = _tileSize;

    auto jAtlases = nlohmann::json::array();
    for (const auto& e : _atlases) {
        nlohmann::json a;
        a["base_id"] = e.BaseId;
        a["path"]    = e.JsonPath;
        a["sampler"] = e.SamplerName;
        jAtlases.push_back(std::move(a));
    }
    j["atlases"] = std::move(jAtlases);

    auto jChunks = nlohmann::json::array();
    for (const auto& [key, chunk] : _chunks) {
        // Decode chunk coordinates from the packed key.
        const i32 cx = static_cast<i32>(static_cast<u32>(key >> 32));
        const i32 cy = static_cast<i32>(static_cast<u32>(key));

        nlohmann::json c;
        c["x"] = cx;
        c["y"] = cy;

        auto jGround  = nlohmann::json::array();
        auto jObjects = nlohmann::json::array();
        auto jOverlay = nlohmann::json::array();
        for (u32 i = 0; i < ChunkSize * ChunkSize; ++i) {
            jGround.push_back(chunk.Tiles[0][i]);
            jObjects.push_back(chunk.Tiles[1][i]);
            jOverlay.push_back(chunk.Tiles[2][i]);
        }
        c["ground"]  = std::move(jGround);
        c["objects"] = std::move(jObjects);
        c["overlay"] = std::move(jOverlay);
        jChunks.push_back(std::move(c));
    }
    j["chunks"] = std::move(jChunks);

    std::ofstream f{std::string(path)};
    if (!f) {
        LOG_WARN(Engine, "TileMap: cannot write '{}'", path);
        return false;
    }
    f << j.dump(2);
    LOG_INFO(Engine, "TileMap: saved '{}' - {} atlases, {} chunks",
             path, _atlases.size(), _chunks.size());
    return true;
}

bool TileMap::LoadAtlasJson(const u32           baseId, const std::string_view  jsonPath,
                            const SamplerHandle sampler, const std::string_view samplerName,
                            TextureManager&     textures)
{
    std::ifstream f{std::string(jsonPath)};
    if (!f) {
        LOG_WARN(Engine, "TileMap: cannot open atlas file '{}'", jsonPath);
        return false;
    }

    nlohmann::json j;
    try { f >> j; }
    catch (const std::exception& e) {
        LOG_WARN(Engine, "TileMap: JSON parse error in '{}': {}", jsonPath, e.what());
        return false;
    }

    const std::string texPath = j.at("texture").get<std::string>();
    const u32         tileW   = j.at("tile_width").get<u32>();
    const u32         tileH   = j.at("tile_height").get<u32>();

    TileAtlas atlas;
    if (!atlas.Load(texPath, tileW, tileH, textures))
        return false;

    // Register per-tile overrides (uv_scroll, solid, etc.).
    for (const auto& t : j.value("tiles", nlohmann::json::array())) {
        const u32 localId = t.at("local_id").get<u32>();
        TileDef   def{};
        if (t.contains("solid")) def.Solid = t["solid"].get<bool>();
        if (t.contains("blocks_light")) def.BlocksLight = t["blocks_light"].get<bool>();
        if (t.contains("flat")) def.Flat = t["flat"].get<bool>();
        if (t.contains("uv_scroll")) {
            def.UVScroll.X = t["uv_scroll"].value("x", 0.0f);
            def.UVScroll.Y = t["uv_scroll"].value("y", 0.0f);
        }
        RegisterTile(baseId + localId, std::move(def));
    }

    RegisterAtlas(baseId, std::move(atlas), sampler, jsonPath, samplerName);
    return true;
}

u64 TileMap::ChunkKey(const i32 chunkX, const i32 chunkY)
{
    return (static_cast<u64>(static_cast<u32>(chunkX)) << 32) |
            static_cast<u64>(static_cast<u32>(chunkY));
}

TileChunk& TileMap::GetOrCreateChunk(const i32 chunkX, const i32 chunkY)
{
    const u64  key   = ChunkKey(chunkX, chunkY);
    const bool isNew = (!_chunks.contains(key));
    TileChunk& chunk = _chunks[key];

    if (isNew) {
        // Tile origin = top-left corner of first tile in this chunk.
        const f32 worldX = static_cast<f32>(chunkX * static_cast<i32>(ChunkSize)) * _tileSize;
        const f32 worldY = static_cast<f32>(chunkY * static_cast<i32>(ChunkSize)) * _tileSize;
        const f32 extent = static_cast<f32>(ChunkSize) * _tileSize;
        LOG_DEBUG(Engine, "TileMap: new chunk ({},{}) - world [{:.0f},{:.0f}] to [{:.0f},{:.0f}]",
                  chunkX, chunkY, worldX, worldY, worldX + extent, worldY + extent);
    }

    return chunk;
}

const TileChunk* TileMap::GetChunk(const i32 chunkX, const i32 chunkY) const
{
    const auto it = _chunks.find(ChunkKey(chunkX, chunkY));
    return it != _chunks.end() ? &it->second : nullptr;
}
} // namespace Arcbit
