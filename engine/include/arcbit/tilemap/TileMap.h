#pragma once

#include <arcbit/tilemap/TileAtlas.h>
#include <arcbit/tilemap/TileDef.h>
#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/render/RenderHandle.h>

#include <unordered_map>
#include <vector>

namespace Arcbit {

// 16×16 grid of tile IDs per layer. ID 0 = empty cell.
constexpr u32 ChunkSize  = 16;
constexpr u32 LayerCount = 3;

struct TileChunk
{
    u32 Tiles[LayerCount][ChunkSize * ChunkSize] = {};
};

// Associates an atlas with the contiguous tile ID range it owns.
// Tile IDs [BaseId, next entry BaseId - 1] belong to this atlas.
struct TileAtlasEntry
{
    u32           BaseId;
    TileAtlas     Atlas;
    SamplerHandle Sampler; // sampler used when rendering tiles from this atlas
};

// Chunk-based infinite tile map with multi-atlas support.
// Tile IDs are contiguous integer ranges partitioned across registered atlases.
// ID 0 is always empty. Atlases must be registered before SetTile calls.
class TileMap
{
public:
    void SetTileSize(f32 size) { _tileSize = size; }
    [[nodiscard]] f32 GetTileSize() const { return _tileSize; }

    // Register a texture atlas and the base tile ID it starts at.
    // baseId must be > 0 and unique. Atlases are sorted internally by BaseId.
    void RegisterAtlas(u32 baseId, TileAtlas atlas, SamplerHandle sampler);

    // Register a TileDef for a specific tile ID.
    // Only needed for tiles that have UV scroll, flip-book animation, or solid flags.
    void RegisterTile(u32 tileId, TileDef def);

    // Set/get a tile ID in a layer (0=Ground, 1=Objects, 2=Overlay).
    void SetTile(i32 tileX, i32 tileY, u32 layerIndex, u32 tileId);
    [[nodiscard]] u32 GetTile(i32 tileX, i32 tileY, u32 layerIndex) const;

    // World-space center of a tile.
    [[nodiscard]] Vec2 TileToWorld(i32 tileX, i32 tileY) const;

    // Tile coordinate containing a world-space point.
    void WorldToTile(Vec2 worldPos, i32& outX, i32& outY) const;

    [[nodiscard]] bool IsSolid(i32 tileX, i32 tileY) const;

    // Read-only access used by the render system.
    [[nodiscard]] const std::unordered_map<u64, TileChunk>& GetChunks()            const;
    [[nodiscard]] const TileAtlasEntry*                      FindAtlas(u32 tileId)  const;
    [[nodiscard]] const TileDef*                             FindTileDef(u32 tileId) const;

    // Log a summary of the current map state: chunk count, tile counts per layer,
    // registered atlases, and registered tile defs. Use after map generation.
    void LogStats() const;

private:
    [[nodiscard]] static u64 ChunkKey(i32 chunkX, i32 chunkY);
    TileChunk&               GetOrCreateChunk(i32 chunkX, i32 chunkY);
    [[nodiscard]] const TileChunk* GetChunk(i32 chunkX, i32 chunkY) const;

    f32 _tileSize = 32.0f;

    // Sorted ascending by BaseId for binary-search lookups.
    std::vector<TileAtlasEntry>       _atlases;
    std::unordered_map<u32, TileDef>  _tileDefs;
    std::unordered_map<u64, TileChunk> _chunks;
};

} // namespace Arcbit
