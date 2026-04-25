#pragma once

#include <arcbit/tilemap/TileAtlas.h>
#include <arcbit/tilemap/TileDef.h>
#include <arcbit/core/Math.h>
#include <arcbit/core/Types.h>
#include <arcbit/physics/TileColliderRect.h>
#include <arcbit/render/RenderHandle.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Arcbit
{
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
    SamplerHandle Sampler;     // sampler used when rendering tiles from this atlas
    std::string   JsonPath;    // path to the .tileatlas.json (empty if not file-loaded)
    std::string   SamplerName; // "nearest" or "linear" — written into .arcmap
};

// Chunk-based infinite tile map with multi-atlas support.
// Tile IDs are contiguous integer ranges partitioned across registered atlases.
// ID 0 is always empty. Atlases must be registered before SetTile calls.
class TileMap
{
public:
    void              SetTileSize(const f32 size) { _tileSize = size; }
    [[nodiscard]] f32 GetTileSize() const { return _tileSize; }

    // Register a texture atlas and the base tile ID it starts at.
    // baseId must be > 0 and unique. Atlases are sorted internally by BaseId.
    // jsonPath/samplerName are stored so SaveMap can write them into the .arcmap.
    void RegisterAtlas(u32              baseId, TileAtlas               atlas, SamplerHandle sampler,
                       std::string_view jsonPath = {}, std::string_view samplerName = "nearest");

    // Register a TileDef for a specific tile ID.
    // Only needed for tiles that have UV scroll, flip-book animation, or solid flags.
    void RegisterTile(u32 tileId, TileDef def);

    // Set/get a tile ID in a layer (0=Ground, 1=Objects, 2=Overlay).
    void              SetTile(i32 tileX, i32 tileY, u32 layerIndex, u32 tileId);
    [[nodiscard]] u32 GetTile(i32 tileX, i32 tileY, u32 layerIndex) const;

    // World-space center of a tile.
    [[nodiscard]] Vec2 TileToWorld(i32 tileX, i32 tileY) const;

    // Tile coordinate containing a world-space point.
    void WorldToTile(Vec2 worldPos, i32& outX, i32& outY) const;

    [[nodiscard]] bool IsSolid(i32 tileX, i32 tileY) const;
    [[nodiscard]] bool BlocksLight(i32 tileX, i32 tileY) const;

    // UV rect for a tile ID's static position in its atlas.
    // Equivalent to FindAtlas(tileId)->Atlas.GetUV(col, row) without manual math.
    [[nodiscard]] UVRect GetTileUV(u32 tileId) const;

    // Read-only access used by the render system.
    [[nodiscard]] const std::unordered_map<u64, TileChunk>& GetChunks() const;
    [[nodiscard]] const TileAtlasEntry*                     FindAtlas(u32 tileId) const;
    [[nodiscard]] const TileDef*                            FindTileDef(u32 tileId) const;

    // ---- Tile collision (Phase 22A) ---------------------------------------
    // Returns the greedy-meshed solid rectangles for the given chunk. Built
    // lazily on first request and cached; the cache for a chunk is invalidated
    // automatically by SetTile when that chunk changes. Empty result for chunks
    // with no solid tiles or chunks that have not been allocated.
    [[nodiscard]] const std::vector<TileColliderRect>& GetChunkColliders(i32 chunkX,
                                                                         i32 chunkY) const;

    // Constants exposed so PhysicsWorld can compute the chunk range overlapping
    // a query AABB without duplicating the chunk-coord math.
    [[nodiscard]] static constexpr u32 GetChunkSize() { return ChunkSize; }

    // Log a summary of the current map state: chunk count, tile counts per layer,
    // registered atlases, and registered tile defs. Use after map generation.
    void LogStats() const;

    // Load a .arcmap JSON file: registers all atlases (via embedded .tileatlas.json paths),
    // sets tile_size, and populates chunk data. "nearest"/"linear" in the arcmap sampler
    // field is mapped to nearestSampler/linearSampler respectively.
    bool LoadMap(std::string_view path, TextureManager&         textures,
                 SamplerHandle    nearestSampler, SamplerHandle linearSampler);

    // Serialize the current map state (atlases + all chunk data) to a .arcmap JSON file.
    // Atlas entries must have been registered with jsonPath/samplerName for the output to
    // be useful as a round-trip input to LoadMap.
    bool SaveMap(std::string_view path) const; // NOLINT(*-use-nodiscard)

    // Parse a .tileatlas.json, load its texture, register any TileDefs, and call RegisterAtlas.
    // Use this instead of RegisterAtlas when the tileatlas JSON defines tile properties
    // (solid, blocks_light, uv_scroll) — RegisterAtlas alone does not read the JSON.
    bool LoadAtlasJson(u32             baseId, std::string_view  jsonPath,
                       SamplerHandle   sampler, std::string_view samplerName,
                       TextureManager& textures);

private:
    [[nodiscard]] static u64       ChunkKey(i32 chunkX, i32 chunkY);
    TileChunk&                     GetOrCreateChunk(i32 chunkX, i32 chunkY);
    [[nodiscard]] const TileChunk* GetChunk(i32 chunkX, i32 chunkY) const;

    // Picks the first solid TileDef across layers (Ground → Objects → Overlay)
    // for one cell, or nullptr if no layer is solid. Used by the greedy mesher.
    [[nodiscard]] const TileDef* GetCollisionDef(i32 tileX, i32 tileY) const;

    // Build (or rebuild) the greedy-meshed collider list for one chunk.
    void BuildChunkColliders(i32 chunkX, i32 chunkY,
                             std::vector<TileColliderRect>& out) const;

    f32 _tileSize = 32.0f;

    // Sorted ascending by BaseId for binary-search lookups.
    std::vector<TileAtlasEntry>        _atlases;
    std::unordered_map<u32, TileDef>   _tileDefs;
    std::unordered_map<u64, TileChunk> _chunks;

    // Per-chunk greedy-meshed collider cache. `mutable` so GetChunkColliders
    // can lazy-fill while staying const. Invalidated by SetTile.
    mutable std::unordered_map<u64, std::vector<TileColliderRect>> _chunkColliders;
};
} // namespace Arcbit
