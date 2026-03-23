#include "ECS.h"

using namespace ECS;

Entity* Chunk::Entities()
{
    return reinterpret_cast<Entity*>(buffer);
}
Entity& Chunk::EntityAt(std::size_t row)
{
    return Entities()[row];
}
void Chunk::SetEntity(std::size_t row, Entity e)
{
    Entities()[row] = e;
}

void* Chunk::ColumnPtr(const ColumnDesc& col)
{
    return buffer + col.offset;
}
void* Chunk::RowPtr(const ColumnDesc& col, std::size_t row)
{
    return static_cast<std::byte*>(ColumnPtr(col)) + row * col.size;
}
const void* Chunk::RowPtr(const ColumnDesc& col, std::size_t row) const
{
    return static_cast<const std::byte*>(buffer + col.offset) + row * col.size;
}

void Chunk::CopyRow(std::size_t src_row, Chunk& dst, std::size_t dst_row, const std::vector<ColumnDesc>& descs)
{
    for (auto& d : descs)
        std::memcpy(dst.RowPtr(d, dst_row), RowPtr(d, src_row), d.size);
}
void Chunk::MoveLastRowTo(std::size_t dst_row, Chunk& dst, const std::vector<ColumnDesc>& descs)
{
    CopyRow(count - 1, dst, dst_row, descs);
}

bool Chunk::Full()  const noexcept { return count == capacity; }
bool Chunk::Empty() const noexcept { return count == 0; }



Archetype::Archetype(ComponentMask _mask, const std::vector<ComponentInfo>& components)
    : mask(_mask)
{
    std::size_t bytes_per_entity = sizeof(Entity);
    for (auto& c : components)
        bytes_per_entity += c.size;

    chunk_capacity = CHUNK_SIZE / bytes_per_entity;
    assert(chunk_capacity > 0 && "Component set too large for chunk size");

    std::size_t offset = chunk_capacity * sizeof(Entity);
    for (auto& c : components)
    {
        descs.push_back({ c.id, c.size, offset });
        offset += chunk_capacity * c.size;
    }
}

const ColumnDesc* Archetype::GetDesc(ComponentId id) const
{
    for (auto& d : descs)
        if (d.id == id) return &d;
    return nullptr;
}

bool Archetype::HasComponent(ComponentId id) const
{
    return HasComponents(MakeMaskFromId(id));
}
bool Archetype::HasComponents(ComponentMask m) const
{
    return (mask & m) == m;
}

void* Archetype::GetPtr(std::size_t chunk_idx, std::size_t row, ComponentId id)
{
    const ColumnDesc* d = GetDesc(id);
    assert(d && "Component not in archetype");
    return chunks[chunk_idx].RowPtr(*d, row);
}
Entity& Archetype::GetEntity(std::size_t chunk_idx, std::size_t row)
{
    return chunks[chunk_idx].EntityAt(row);
}

void Archetype::Write(std::size_t chunk_idx, std::size_t row, ComponentId id, const void* src)
{
    const ColumnDesc* d = GetDesc(id);
    assert(d && "Component not in archetype");
    std::memcpy(chunks[chunk_idx].RowPtr(*d, row), src, d->size);
}

Chunk& Archetype::GetOrAddChunk()
{
    for (auto& c : chunks)
        if (!c.Full()) return c;

    Chunk& c = chunks.emplace_back();
    c.cols = &descs;
    c.capacity = chunk_capacity;
    return c;
}
std::size_t Archetype::ChunkCount() const noexcept
{
    return chunks.size();
}
std::size_t Archetype::EntityCount() const noexcept
{
    std::size_t n = 0;
    for (const auto& c : chunks)
        n += c.count;
    return n;
}

std::pair<std::size_t, std::size_t> Archetype::LastSlot() const
{
    assert(!chunks.empty() && chunks.back().count > 0);
    return { chunks.size() - 1, chunks.back().count - 1 };
}
std::pair<std::size_t, std::size_t> Archetype::AllocateSlot()
{
    Chunk& c = GetOrAddChunk();
    std::size_t i = &c - chunks.data();
    return { i, c.count++ };
}

Entity Archetype::SwapRemove(std::size_t chunk_idx, std::size_t row)
{
    auto [last_ci, last_row] = LastSlot();
    Entity displaced = GetEntity(last_ci, last_row);

    if (chunk_idx != last_ci || row != last_row)
    {
        GetEntity(chunk_idx, row) = displaced;
        chunks[last_ci].MoveLastRowTo(row, chunks[chunk_idx], descs);
    }

    chunks[last_ci].count--;
    if (chunks[last_ci].Empty() && chunks.size() > 1)
        chunks.pop_back();

    return displaced;
}
std::pair<std::size_t, std::size_t> Archetype::MigrateEntityTo(std::size_t chunk_idx, std::size_t row, Archetype& dst)
{
    auto [dst_ci, dst_row] = dst.AllocateSlot();

    for (auto& d : descs)
    {
        const ColumnDesc* dst_desc = dst.GetDesc(d.id);
        if (!dst_desc) continue;
        std::memcpy(
            dst.chunks[dst_ci].RowPtr(*dst_desc, dst_row),
            chunks[chunk_idx].RowPtr(d, row),
            d.size
        );
    }

    return { dst_ci, dst_row };
}

void Archetype::ForEachChunk(const std::function<void(Chunk&, std::size_t)>& fn)
{
    for (std::size_t i = 0; i < chunks.size(); i++)
        if (!chunks[i].Empty())
            fn(chunks[i], i);
}
void Archetype::ForEachEntity(const std::function<void(Entity, std::size_t, std::size_t)>& fn)
{
    ForEachChunk([&](Chunk& c, std::size_t ci)
        {
            for (std::size_t row = 0; row < c.count; row++)
                fn(c.EntityAt(row), ci, row);
        });
}



World::World()
{
    GetOrCreateArchetype(0, {});
}

void World::GrowRecords(uint32_t id) 
{
    if (id >= records_.size()) {
        std::size_t newSize = std::max((std::size_t)id + 1, records_.size() * 2);
        records_.resize(newSize);
    }
}

Archetype& World::GetOrCreateArchetype(ComponentMask mask, const std::vector<ComponentInfo>& info)
{
    auto it = archetypes_.find(mask);
    if (it != archetypes_.end())
        return *it->second;
    auto [ins, ok] = archetypes_.emplace(mask, std::make_unique<Archetype>(mask, info));
    return *ins->second;
}
Archetype& World::GetOrCreateEdge(Archetype& arch, ComponentId id, std::size_t size, bool adding)
{
    std::unordered_map<ComponentId, Archetype*>* edge_map;
    if (adding)
        edge_map = &arch.add_edge;
    else
        edge_map = &arch.remove_edge;

    auto it = edge_map->find(id);
    if (it != edge_map->end())
        return *it->second;

    ComponentMask new_mask;
    if (adding)
        new_mask = arch.mask | MakeMaskFromId(id);
    else
        new_mask = arch.mask & ~MakeMaskFromId(id);

    Archetype* new_arch = nullptr;
    auto existing = archetypes_.find(new_mask);
    if (existing != archetypes_.end())
    {
        new_arch = existing->second.get();
    }
    else
    {
        std::vector<ComponentInfo> info;
        for (auto& d : arch.descs)
        {
            if (adding || d.id != id)
                info.push_back({ d.id, d.size });
        }
        if (adding)
            info.push_back({ id, size });

        new_arch = &GetOrCreateArchetype(new_mask, info);
    }

    (*edge_map)[id] = new_arch;
    return *new_arch;
}

Entity World::Create()
{
    uint32_t id;
    if (!free_.empty())
    {
        id = free_.back();
        free_.pop_back();
    }
    else
        id = next_++;

    GrowRecords(id);
    Archetype& arch = *archetypes_.at(0);
    auto [ci, row] = arch.AllocateSlot();
    Entity e = MakeEntity(id, records_[id].generation);
    arch.GetEntity(ci, row) = e;
    records_[id] = { &arch, ci, row, records_[id].generation };
    return e;
}
void World::Destroy(Entity e)
{
    uint32_t id = EntityId(e);
    assert(id < records_.size() && Alive(e));
    auto& rec = records_[id];
    Entity moved = rec.arch->SwapRemove(rec.chunk_idx, rec.row);
    if (moved != e)
    {
        uint32_t moved_id = EntityId(moved);
        records_[moved_id].arch = rec.arch;
        records_[moved_id].chunk_idx = rec.chunk_idx;
        records_[moved_id].row = rec.row;
    }
    uint32_t next_gen = rec.generation + 1;
    records_[id] = { nullptr, 0, 0, next_gen };
    free_.push_back(id);
}
bool World::Alive(Entity e) const noexcept
{
    uint32_t id = EntityId(e);
    if (id >= records_.size())
        return false;
    if (records_[id].arch == nullptr)
        return false;
    return records_[id].generation == EntityGeneration(e);
}

void World::EnableSystem(StringId name)
{
    for (auto& s : update_systems_)
        if (s.name == name) {
            s.enabled = true;
            systems_dirty_ = true; 
            return;
        }
    for (auto& s : render_systems_)
        if (s.name == name) {
            s.enabled = true;
            systems_dirty_ = true;
            return;
        }
}
void World::DisableSystem(StringId name)
{
    for (auto& s : update_systems_)
        if (s.name == name) {
            s.enabled = false;
            systems_dirty_ = true;
            return;
        }
    for (auto& s : render_systems_)
        if (s.name == name) {
            s.enabled = false;
            systems_dirty_ = true;
            return;
        }
}

void World::RebuildFusedGroups(std::vector<SystemEntry>& systems, std::vector<FusedGroup>& groups)
{
    groups.clear();

    for (auto& sys : systems)
    {
        if (!sys.enabled) continue;

        bool fits = !groups.empty();
        if (fits)
        {
            const auto& last = groups.back();
            if ((sys.write_mask & last.read_mask) != 0)
                fits = false;
            if ((sys.write_mask & last.write_mask) != 0)
                fits = false;
            if ((sys.read_mask & last.write_mask) != 0)
                fits = false;
        }

        if (!fits)
            groups.emplace_back();

        auto& grp = groups.back();
        grp.systems.push_back(&sys);
        grp.read_mask |= sys.read_mask;
        grp.write_mask |= sys.write_mask;
    }
}

void World::RunSystems(std::vector<FusedGroup>& groups, float dt)
{
    for (auto& group : groups)
    {
        for (auto& [mask, arch_ptr] : archetypes_)
        {
            for (std::size_t i = 0; i < arch_ptr->chunks.size(); i++)
            {
                if (arch_ptr->chunks[i].Empty()) 
                    continue;
                for (auto* sys : group.systems)
                {
                    if (!arch_ptr->HasComponents(sys->mask)) 
                        continue;
                    ArchetypeContext ctx{ arch_ptr.get(), i, this };
                    sys->fn(ctx, dt, _data);
                }
            }
        }
    }
}

void World::Run(SystemGroup group, float dt)
{
    if (systems_dirty_)
    {
        RebuildFusedGroups(update_systems_, update_fused_);
        RebuildFusedGroups(render_systems_, render_fused_);
        systems_dirty_ = false;
    }

    if (group == SystemGroup::Update)
        RunSystems(update_fused_, dt);
    else
        RunSystems(render_fused_, dt);
}