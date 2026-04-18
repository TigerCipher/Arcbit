#include <arcbit/tilemap/TileMap.h>
#include <arcbit/core/Log.h>

#include <algorithm>
#include <cmath>

namespace Arcbit {

void TileMap::RegisterAtlas(const u32 baseId, TileAtlas atlas, const SamplerHandle sampler)
{
    // Keep _atlases sorted by BaseId for binary search in FindAtlas.
    auto it = std::lower_bound(_atlases.begin(), _atlases.end(), baseId,
                               [](const TileAtlasEntry& e, u32 id) { return e.BaseId < id; });

    // Compute the inclusive ID range this atlas owns (up to the next atlas BaseId - 1).
    const u32 tileCount = atlas.Columns() * atlas.Rows();
    const u32 endId     = baseId + tileCount - 1;
    LOG_INFO(Engine, "TileMap: registered atlas base={} end={} ({} tiles, {}x{})",
             baseId, endId, tileCount, atlas.Columns(), atlas.Rows());

    _atlases.insert(it, TileAtlasEntry{ baseId, std::move(atlas), sampler });
}

void TileMap::RegisterTile(const u32 tileId, TileDef def)
{
    _tileDefs[tileId] = std::move(def);
}

void TileMap::SetTile(const i32 tileX, const i32 tileY, const u32 layerIndex, const u32 tileId)
{
    const i32 chunkX    = static_cast<i32>(std::floor(static_cast<f32>(tileX) / ChunkSize));
    const i32 chunkY    = static_cast<i32>(std::floor(static_cast<f32>(tileY) / ChunkSize));
    const u32 localX    = static_cast<u32>(tileX - chunkX * static_cast<i32>(ChunkSize));
    const u32 localY    = static_cast<u32>(tileY - chunkY * static_cast<i32>(ChunkSize));
    const u32 cellIndex = localY * ChunkSize + localX;

    TileChunk& chunk = GetOrCreateChunk(chunkX, chunkY);
    chunk.Tiles[layerIndex][cellIndex] = tileId;
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
    return { static_cast<f32>(tileX) * _tileSize, static_cast<f32>(tileY) * _tileSize };
}

void TileMap::WorldToTile(const Vec2 worldPos, i32& outX, i32& outY) const
{
    outX = static_cast<i32>(std::floor(worldPos.X / _tileSize));
    outY = static_cast<i32>(std::floor(worldPos.Y / _tileSize));
}

bool TileMap::IsSolid(const i32 tileX, const i32 tileY) const
{
    for (u32 layer = 0; layer < LayerCount; ++layer)
    {
        const u32 id = GetTile(tileX, tileY, layer);
        if (id == 0) continue;
        const TileDef* def = FindTileDef(id);
        if (def && def->Solid) return true;
    }
    return false;
}

const std::unordered_map<u64, TileChunk>& TileMap::GetChunks() const
{
    return _chunks;
}

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

void TileMap::LogStats() const
{
    u32 tileCounts[LayerCount] = {};
    for (const auto& [key, chunk] : _chunks)
    {
        for (u32 layer = 0; layer < LayerCount; ++layer)
            for (u32 cell = 0; cell < ChunkSize * ChunkSize; ++cell)
                if (chunk.Tiles[layer][cell] != 0)
                    ++tileCounts[layer];
    }

    LOG_INFO(Engine, "TileMap stats: {} chunks | tileSize={:.0f}",
             _chunks.size(), _tileSize);
    LOG_INFO(Engine, "  Layer 0 (Ground):  {} tiles set", tileCounts[0]);
    LOG_INFO(Engine, "  Layer 1 (Objects): {} tiles set", tileCounts[1]);
    LOG_INFO(Engine, "  Layer 2 (Overlay): {} tiles set", tileCounts[2]);
    LOG_INFO(Engine, "  Atlases: {} registered | TileDefs: {} registered",
             _atlases.size(), _tileDefs.size());

    for (const auto& entry : _atlases)
    {
        const u32 tileCount = entry.Atlas.Columns() * entry.Atlas.Rows();
        LOG_INFO(Engine, "  Atlas baseId={} - {}x{} grid - IDs {}-{}",
                 entry.BaseId, entry.Atlas.Columns(), entry.Atlas.Rows(),
                 entry.BaseId, entry.BaseId + tileCount - 1);
    }
}

u64 TileMap::ChunkKey(const i32 chunkX, const i32 chunkY)
{
    return (static_cast<u64>(static_cast<u32>(chunkX)) << 32) |
            static_cast<u64>(static_cast<u32>(chunkY));
}

TileChunk& TileMap::GetOrCreateChunk(const i32 chunkX, const i32 chunkY)
{
    const u64 key = ChunkKey(chunkX, chunkY);
    const bool isNew = (_chunks.find(key) == _chunks.end());
    TileChunk& chunk = _chunks[key];

    if (isNew)
    {
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
