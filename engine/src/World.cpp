#include <arcbit/ecs/World.h>
#include <arcbit/core/Assert.h>
#include <arcbit/core/Log.h>

#include <cmath>
#include <ranges>

namespace Arcbit {

// ---------------------------------------------------------------------------
// Internal — component type registry
// ---------------------------------------------------------------------------
namespace Internal {

static std::vector<std::function<ComponentArray()>>& GetRegistry()
{
    static std::vector<std::function<ComponentArray()>> r;
    return r;
}

ComponentTypeID AllocID()
{
    GetRegistry().emplace_back();
    return static_cast<ComponentTypeID>(GetRegistry().size() - 1);
}

void RegisterFactory(const ComponentTypeID id, std::function<ComponentArray()> factory)
{
    GetRegistry()[id] = std::move(factory);
}

ComponentArray CreateArray(const ComponentTypeID id)
{
    ARCBIT_ASSERT(id < GetRegistry().size() && GetRegistry()[id], "CreateArray: unknown component type");
    return GetRegistry()[id]();
}

} // namespace Internal

// ---------------------------------------------------------------------------
// ComponentArray
// ---------------------------------------------------------------------------

void ComponentArray::PushDefault()
{
    _data.resize(_data.size() + _elementSize);
    _defaultConstruct(GetRaw(_count));
    ++_count;
}

void ComponentArray::MoveFrom(const u32 srcIndex, ComponentArray& src)
{
    _data.resize(_data.size() + _elementSize);
    _moveConstruct(GetRaw(_count), src.GetRaw(srcIndex));
    ++_count;
}

void ComponentArray::SwapRemove(const u32 index)
{
    ARCBIT_ASSERT(index < _count, "SwapRemove: index out of range");
    const u32 last = _count - 1;
    if (index != last) {
        _destruct(GetRaw(index));
        _moveConstruct(GetRaw(index), GetRaw(last));
    }
    _destruct(GetRaw(last));
    _data.resize(_data.size() - _elementSize);
    --_count;
}

// ---------------------------------------------------------------------------
// World — private helpers
// ---------------------------------------------------------------------------

Archetype& World::GetOrCreateArchetype(ComponentMask mask)
{
    auto it = _archetypes.find(mask);
    if (it != _archetypes.end())
        return it->second;

    Archetype& arch = _archetypes.emplace(mask, Archetype{}).first->second;
    arch.Mask = mask;
    for (ComponentTypeID i = 0; i < 64; ++i)
        if (mask & (1ULL << i))
            arch.Columns.emplace(i, Internal::CreateArray(i));
    return arch;
}

void World::SwapRemoveFromArchetype(Archetype& arch, const u32 slot)
{
    const u32 lastSlot = static_cast<u32>(arch.Entities.size()) - 1;

    for (auto& [id, col] : arch.Columns)
        col.SwapRemove(slot);

    if (slot != lastSlot) {
        arch.Entities[slot] = arch.Entities[lastSlot];
        _entityTable[arch.Entities[slot].Index].slot = slot;
    }
    arch.Entities.pop_back();
}

// ---------------------------------------------------------------------------
// World — public API
// ---------------------------------------------------------------------------

Entity World::CreateEntity()
{
    const ComponentTypeID transformID = TypeOf<Transform2D>();
    const ComponentTypeID tagID       = TypeOf<Tag>();
    const ComponentMask   mask        = (1ULL << transformID) | (1ULL << tagID);

    Archetype& arch = GetOrCreateArchetype(mask);

    u32 index, generation;
    if (!_freeList.empty()) {
        index      = _freeList.back();
        generation = _entityTable[index].generation;
        _freeList.pop_back();
    } else {
        index      = static_cast<u32>(_entityTable.size());
        generation = 0;
        _entityTable.push_back({});
    }

    const Entity entity = { index, generation };
    const u32    slot   = static_cast<u32>(arch.Entities.size());

    for (auto& col : arch.Columns | std::views::values)
        col.PushDefault();
    arch.Entities.push_back(entity);

    _entityTable[index] = { mask, slot, generation };
    return entity;
}

void World::DestroyEntity(const Entity entity)
{
    if (!IsValid(entity)) return;
    auto& rec = _entityTable[entity.Index];
    SwapRemoveFromArchetype(_archetypes.at(rec.mask), rec.slot);
    ++rec.generation;
    rec.mask = 0;
    rec.slot = 0;
    _freeList.push_back(entity.Index);
}

bool World::IsValid(const Entity entity) const
{
    if (entity.Index == ~0u || entity.Index >= _entityTable.size())
        return false;
    return _entityTable[entity.Index].generation == entity.Generation;
}

void World::RegisterSystem(const std::string_view name, UpdateFn fn)
{
    _updateSystems.emplace_back(std::string(name), std::move(fn));
}

void World::RegisterRenderSystem(const std::string_view name, RenderFn fn)
{
    _renderSystems.emplace_back(std::string(name), std::move(fn));
}

void World::RunUpdateSystems(Scene& scene, const f32 dt)
{
    for (auto& fn : _updateSystems | std::views::values)
        fn(scene, dt);
}

void World::RunRenderSystems(Scene& scene, FramePacket& packet)
{
    for (auto& fn : _renderSystems | std::views::values)
        fn(scene, packet);
}

Transform2D* World::GetTransform(const Entity entity) { return GetComponent<Transform2D>(entity); }

} // namespace Arcbit
