#include <arcbit/physics/SpatialHash.h>

#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

#include <algorithm>
#include <cmath>

namespace Arcbit
{
SpatialHash::SpatialHash(const f32 cellSize) : _cellSize(cellSize),
                                               _invCellSize(1.0f / cellSize)
{
    ARCBIT_ASSERT(cellSize > 0.0f, "SpatialHash cell size must be positive");
}

void SpatialHash::Insert(const ColliderId id, const AABB& aabb)
{
    ARCBIT_ASSERT(aabb.IsValid(), "SpatialHash::Insert called with invalid AABB");
    const auto [X0, Y0, X1, Y1] = CellsOf(aabb);
    for (i32 y = Y0; y <= Y1; ++y)
        for (i32 x = X0; x <= X1; ++x)
            _cells[PackKey(x, y)].push_back(id);
}

void SpatialHash::Remove(const ColliderId id, const AABB& aabb)
{
    ARCBIT_ASSERT(aabb.IsValid(), "SpatialHash::Remove called with invalid AABB");
    const auto [X0, Y0, X1, Y1] = CellsOf(aabb);
    for (i32 y = Y0; y <= Y1; ++y) {
        for (i32 x = X0; x <= X1; ++x) {
            const auto it = _cells.find(PackKey(x, y));
            if (it == _cells.end()) continue;
            auto& bucket = it->second;
            // swap-and-pop the first matching id; an id can only appear once per cell
            if (const auto idIt = std::find(bucket.begin(), bucket.end(), id); idIt != bucket.end()) {
                *idIt = bucket.back();
                bucket.pop_back();
            }
            if (bucket.empty()) _cells.erase(it);
        }
    }
}

void SpatialHash::Clear() noexcept { _cells.clear(); }

void SpatialHash::Query(const AABB& queryAABB, std::vector<ColliderId>& out) const
{
    out.clear();
    if (!queryAABB.IsValid()) return;

    const CellRange r = CellsOf(queryAABB);
    for (i32 y = r.Y0; y <= r.Y1; ++y) {
        for (i32 x = r.X0; x <= r.X1; ++x) {
            const auto it = _cells.find(PackKey(x, y));
            if (it == _cells.end()) continue;
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }

    // Dedupe — colliders that span multiple cells will appear once per cell.
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
}

SpatialHash::CellRange SpatialHash::CellsOf(const AABB& aabb) const noexcept
{
    // floor(min*inv) and floor(max*inv) — std::floor handles negatives correctly
    // (truncation toward zero would map -0.5 to 0, which would put cell -1 work
    // into cell 0 and break negative-coordinate queries).
    return {
        static_cast<i32>(std::floor(aabb.Min.X * _invCellSize)),
        static_cast<i32>(std::floor(aabb.Min.Y * _invCellSize)),
        static_cast<i32>(std::floor(aabb.Max.X * _invCellSize)),
        static_cast<i32>(std::floor(aabb.Max.Y * _invCellSize)),
    };
}

// ---------------------------------------------------------------------------
// SelfTest — round-trip insert/query/remove sanity check, debug builds only.
// ---------------------------------------------------------------------------

void SpatialHash::SelfTest()
{
    #ifdef ARCBIT_DEBUG
    SpatialHash h(32.0f);

    // Single-cell AABB at world (16, 16).
    constexpr AABB a = AABB::FromCenterHalfExtents({16.0f, 16.0f}, {8.0f, 8.0f});
    // Single-cell AABB two cells east at world (80, 16) — cell (2, 0).
    constexpr AABB b = AABB::FromCenterHalfExtents({80.0f, 16.0f}, {8.0f, 8.0f});
    // Multi-cell AABB straddling cells (0,0) (1,0) (0,1) (1,1).
    constexpr AABB c = AABB::FromCenterHalfExtents({32.0f, 32.0f}, {16.0f, 16.0f});

    h.Insert(1u, a);
    h.Insert(2u, b);
    h.Insert(3u, c);

    std::vector<ColliderId> result;

    // Query overlapping with `a` should return ids 1 and 3, not 2.
    h.Query(a, result);
    ARCBIT_ASSERT(std::ranges::find(result, 1u) != result.end(),
                  "SpatialHash::SelfTest: id 1 missing from cell (0,0) query");
    ARCBIT_ASSERT(std::ranges::find(result, 3u) != result.end(),
                  "SpatialHash::SelfTest: id 3 missing from cell (0,0) query");
    ARCBIT_ASSERT(std::ranges::find(result, 2u) == result.end(),
                  "SpatialHash::SelfTest: id 2 leaked into unrelated cell");

    // Query at `b` should return only id 2.
    h.Query(b, result);
    ARCBIT_ASSERT(result.size() == 1 && result[0] == 2u,
                  "SpatialHash::SelfTest: cell (2,0) query returned wrong set");

    // Wide query crossing all three colliders' cells must return each id exactly once.
    constexpr AABB wide = AABB::FromCenterHalfExtents({40.0f, 40.0f}, {64.0f, 64.0f});
    h.Query(wide, result);
    {
        auto sorted = result;
        std::ranges::sort(sorted);
        const auto last = std::ranges::unique(sorted).begin();
        ARCBIT_ASSERT(last == sorted.end(),
                      "SpatialHash::SelfTest: query returned duplicate ids");
    }

    // Remove(1) from `a`, then re-query — id 1 must be gone, id 3 still present.
    h.Remove(1u, a);
    h.Query(a, result);
    ARCBIT_ASSERT(std::ranges::find(result, 1u) == result.end(),
                  "SpatialHash::SelfTest: id 1 still present after Remove");
    ARCBIT_ASSERT(std::ranges::find(result, 3u) != result.end(),
                  "SpatialHash::SelfTest: Remove(1) corrupted unrelated id 3");

    // Negative coordinates — common with worlds centered on origin.
    SpatialHash    neg(32.0f);
    constexpr AABB negAABB = AABB::FromCenterHalfExtents({-16.0f, -16.0f}, {8.0f, 8.0f});
    neg.Insert(42u, negAABB);
    neg.Query(negAABB, result);
    ARCBIT_ASSERT(result.size() == 1 && result[0] == 42u,
                  "SpatialHash::SelfTest: negative coordinates broken");

    // Clear must drop everything.
    neg.Clear();
    ARCBIT_ASSERT(neg.CellCount() == 0,
                  "SpatialHash::SelfTest: Clear left cells behind");

    LOG_DEBUG(Engine, "SpatialHash::SelfTest passed");
    #endif
}
} // namespace Arcbit
