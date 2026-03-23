#pragma once
#include <cstddef>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <string_view>
#include <cassert>
#include <cstring>
#include <span>

#if __has_include("Struct.h")
    #include "Struct.h"
#else
struct StringId
{
    uint32_t id = 0;

    StringId() = default;
    constexpr StringId(const char* str) : StringId(std::string_view(str)) {}
    constexpr StringId(std::string_view str)
    {
        uint32_t h = 2166136261u;
        for (char c : str)
            h = (h ^ (uint8_t)c) * 16777619u;
        id = h;
    }

    constexpr bool operator==(const StringId& o) const { return id == o.id; }
    constexpr bool operator!=(const StringId& o) const { return id != o.id; }
};

template<>
struct std::hash<StringId>
{
    size_t operator()(const StringId& s) const noexcept { return s.id; }
};
#endif


struct SharedData;
typedef std::shared_ptr<SharedData> SharedDataRef;

namespace ECS
{
    using Entity = uint64_t;
    using ComponentId = uint32_t;
    using ComponentMask = uint64_t;

    constexpr std::size_t CHUNK_SIZE = 16 * 1024;

    /// <summary>
    /// Get Entity ID of entity
    /// </summary>
    /// <param name="e"></param>
    /// <returns></returns>
    constexpr uint32_t EntityId(Entity e) {
        return (uint32_t)(e >> 32);
    }
    /// <summary>
    /// Get Entity generation of entity
    /// </summary>
    /// <param name="e"></param>
    /// <returns></returns>
    constexpr uint32_t EntityGeneration(Entity e) {
        return (uint32_t)(e & 0xFFFFFFFF);
    }
    /// <summary>
    /// make entity with generation and id
    /// </summary>
    /// <param name="id"></param>
    /// <param name="gen"></param>
    /// <returns></returns>
    constexpr Entity MakeEntity(uint32_t id, uint32_t gen) {
        return ((uint64_t)id << 32) | gen;
    }

    inline ComponentId AllocComponentId() noexcept
    {
        static ComponentId counter = 0;
        return counter++;
    }

    /// <summary>
    /// Get ID of a component
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <returns></returns>
    template<typename T>
    ComponentId GetComponentId() noexcept
    {
        static const ComponentId id = AllocComponentId();
        return id;
    }

    /// <summary>
    /// Get component bit in bitmask
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <returns></returns>
    template<typename T>
    ComponentMask ComponentBit() noexcept
    {
        return ComponentMask(1) << GetComponentId<T>();
    }

    /// <summary>
    /// make bitmask from components
    /// </summary>
    /// <typeparam name="...Ts"></typeparam>
    /// <returns></returns>
    template<typename... Ts>
    ComponentMask MakeMask() noexcept
    {
        return (ComponentBit<Ts>() | ... | ComponentMask(0));
    }

    /// <summary>
    /// make bitmask from component id
    /// </summary>
    /// <param name="id"></param>
    /// <returns></returns>
    inline ComponentMask MakeMaskFromId(ComponentId id) noexcept
    {
        return ComponentMask(1) << id;
    }


    struct ColumnDesc
    {
        ComponentId id;
        std::size_t size;
        std::size_t offset;
    };
    struct ComponentInfo
    {
        ComponentId id;
        std::size_t size;
    };

    class Chunk
    {
    public:
        alignas(64) std::byte buffer[CHUNK_SIZE];
        const std::vector<ColumnDesc>* cols = nullptr;
        std::size_t capacity = 0;
        std::size_t count = 0;

        Entity* Entities();
        Entity& EntityAt(std::size_t row);
        void SetEntity(std::size_t row, Entity e);

        void* ColumnPtr(const ColumnDesc& col);
        void* RowPtr(const ColumnDesc& col, std::size_t row);
        const void* RowPtr(const ColumnDesc& col, std::size_t row) const;

        void CopyRow(std::size_t src_row, Chunk& dst, std::size_t dst_row, const std::vector<ColumnDesc>& descs);
        void MoveLastRowTo(std::size_t dst_row, Chunk& dst, const std::vector<ColumnDesc>& descs);

        template<typename T>
        T* GetArray(ComponentId id)
        {
            for (auto& col : *cols)
                if (col.id == id)
                    return reinterpret_cast<T*>(buffer + col.offset);
            return nullptr;
        }

        template<typename T>
        T& Get(std::size_t row)
        {
            return *GetArray<T>(GetComponentId<T>());
        }

        bool Full() const noexcept;
        bool Empty() const noexcept;
    };

    class Archetype
    {
    public:
        ComponentMask mask;
        std::vector<ColumnDesc> descs;
        std::size_t chunk_capacity = 0;
        std::vector<Chunk> chunks;

        std::unordered_map<ComponentId, Archetype*> add_edge;
        std::unordered_map<ComponentId, Archetype*> remove_edge;

        Archetype(ComponentMask mask, const std::vector<ComponentInfo>& components);

        const ColumnDesc* GetDesc(ComponentId id) const;
        bool HasComponent(ComponentId id) const;
        bool HasComponents(ComponentMask m) const;

        void* GetPtr(std::size_t chunk_idx, std::size_t row, ComponentId id);
        Entity& GetEntity(std::size_t chunk_idx, std::size_t row);
        void Write(std::size_t chunk_idx, std::size_t row, ComponentId id, const void* src);

        template<typename T>
        T& Get(std::size_t chunk_idx, std::size_t row)
        {
            return *static_cast<T*>(GetPtr(chunk_idx, row, GetComponentId<T>()));
        }

        template<typename T>
        void Set(std::size_t chunk_idx, std::size_t row, const T& val)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            Write(chunk_idx, row, GetComponentId<T>(), &val);
        }

        Chunk& GetOrAddChunk();
        std::size_t ChunkCount() const noexcept;
        std::size_t EntityCount() const noexcept;
        std::pair<std::size_t, std::size_t> LastSlot() const;

        std::pair<std::size_t, std::size_t> AllocateSlot();
        Entity SwapRemove(std::size_t chunk_idx, std::size_t row);
        std::pair<std::size_t, std::size_t> MigrateEntityTo(std::size_t chunk_idx, std::size_t row, Archetype& dst);

        void ForEachChunk(const std::function<void(Chunk&, std::size_t)>& fn);
        void ForEachEntity(const std::function<void(Entity, std::size_t, std::size_t)>& fn);
    };


    enum class SystemGroup { Update, Render };

    class World;

    struct EntityRecord
    {
        Archetype* arch = nullptr;
        std::size_t chunk_idx = 0;
        std::size_t row = 0;
        uint32_t generation = 0;
    };

    struct ArchetypeContext
    {
        Archetype* arch = nullptr;
        std::size_t chunk_idx = 0;
        World* world = nullptr;


        /// <summary>
        /// Returns a list of components T in this archetype
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <returns></returns>
        template<typename T>
        std::span<T> Slice()
        {
            auto& chunk = arch->chunks[chunk_idx];
            if constexpr (std::is_same_v<T, Entity>)
                return std::span<Entity>(chunk.Entities(), chunk.count);
            else
                return std::span<T>(chunk.GetArray<T>(GetComponentId<T>()), chunk.count);
        }

        /// <summary>
        /// Returns a vector of all archetypes with at least components ... Ts
        /// </summary>
        /// <typeparam name="...Ts"></typeparam>
        /// <returns></returns>
        template<typename... Ts>
        std::vector<ArchetypeContext> View();
    };

    using SystemFn = std::function<void(ArchetypeContext&, float, SharedDataRef)>;

    struct SystemEntry
    {
        StringId name;
        ComponentMask mask;
        SystemFn fn;
        bool enabled = true;
        ComponentMask read_mask = ~ComponentMask(0);
        ComponentMask write_mask = ~ComponentMask(0);
    };

    struct FusedGroup
    {
        std::vector<SystemEntry*> systems;
        ComponentMask read_mask = 0;
        ComponentMask write_mask = 0;
    };

    class SystemBuilder
    {
        SystemEntry& entry_;
        bool declared_ = false;

        void EnsureDeclared()
        {
            if (!declared_)
            {
                declared_ = true;
                entry_.read_mask = 0;
                entry_.write_mask = 0;
            }
        }

    public:
        SystemBuilder(SystemEntry& entry) : entry_(entry) {}

        /// <summary>
        /// Adds read dependancy to component T
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <returns></returns>
        template<typename T>
        SystemBuilder& Read()
        {
            EnsureDeclared();
            entry_.read_mask |= ComponentBit<T>();
            return *this;
        }

        /// <summary>
        /// Adds write dependancy to component T
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <returns></returns>
        template<typename T>
        SystemBuilder& Write()
        {
            EnsureDeclared();
            entry_.write_mask |= ComponentBit<T>();
            return *this;
        }
    };


    class World
    {
        std::vector<EntityRecord> records_;
        std::vector<uint32_t> free_;
        uint32_t next_ = 0;

        std::unordered_map<ComponentMask, std::unique_ptr<Archetype>> archetypes_;

        std::vector<SystemEntry> update_systems_;
        std::vector<SystemEntry> render_systems_;

        std::vector<FusedGroup> update_fused_;
        std::vector<FusedGroup> render_fused_;
        bool systems_dirty_ = true;

        Archetype& GetOrCreateArchetype(ComponentMask mask, const std::vector<ComponentInfo>& info);
        Archetype& GetOrCreateEdge(Archetype& arch, ComponentId id, std::size_t size, bool adding);
        void GrowRecords(uint32_t id);
        void RebuildFusedGroups(std::vector<SystemEntry>& systems, std::vector<FusedGroup>& groups);
        void RunSystems(std::vector<FusedGroup>& groups, float dt);

        SharedDataRef _data;

    public:

        World();

        /// <summary>
        /// Create a new ECS entity, returns entity handle ID
        /// </summary>
        /// <returns></returns>
        Entity Create();
        /// <summary>
        /// Destroy enntity with handle
        /// </summary>
        /// <param name="e">Entity handle</param>
        void Destroy(Entity e);
        /// <summary>
        /// Returns true if entity is alive, otherwise false
        /// </summary>
        /// <param name="e">Entity handle</param>
        /// <returns></returns>
        bool Alive(Entity e) const noexcept;

        /// <summary>
        /// Executes a lambda for each entity with components ... Ts
        /// </summary>
        /// <typeparam name="...Ts">Entity components</typeparam>
        /// <typeparam name="Fn"></typeparam>
        /// <param name="fn">lambda</param>
        template<typename... Ts, typename Fn>
        void Query(Fn&& fn)
        {
            ComponentMask mask = MakeMask<Ts...>();
            for (auto& [m, arch] : archetypes_)
            {
                if (!arch->HasComponents(mask)) continue;
                arch->ForEachEntity([&](Entity e, std::size_t ci, std::size_t row)
                    {
                        fn(e, arch->Get<Ts>(ci, row)...);
                    });
            }
        }

        /// <summary>
        /// Tie GameData to ECS world
        /// </summary>
        /// <param name="data"></param>
        void Tie(SharedDataRef data) {
            _data = data;
        }

        /// <summary>
        /// Adds a component to entity
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="e">Entity handle</param>
        /// <param name="value">Initial component values</param>
        template<typename T>
        void Add(Entity e, T value = T{});

        /// <summary>
        /// Adds multiple components to entity
        /// </summary>
        /// <typeparam name="T1"></typeparam>
        /// <typeparam name="T2"></typeparam>
        /// <typeparam name="...Ts"></typeparam>
        /// <param name="e">Entity handle</param>
        /// <param name="v1">Initial component values</param>
        /// <param name="v2"></param>
        /// <param name="...values"></param>
        template<typename T1, typename T2, typename... Ts>
        void Add(Entity e, T1 v1, T2 v2, Ts... values)
        {
            Add<T1>(e, v1);
            Add<T2>(e, v2);
            (Add<Ts>(e, values), ...);
        }

        /// <summary>
        /// Removes a component from entity
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="e">Entity handle</param>
        template<typename T>
        void Remove(Entity e);

        /// <summary>
        /// Removes multiple components from entity
        /// </summary>
        /// <typeparam name="T1"></typeparam>
        /// <typeparam name="T2"></typeparam>
        /// <typeparam name="...Ts"></typeparam>
        /// <param name="e">Entity handle</param>
        template<typename T1, typename T2, typename... Ts>
        void Remove(Entity e)
        {
            Remove<T1>(e);
            Remove<T2>(e);
            (Remove<Ts>(e), ...);
        }

        /// <summary>
        /// Returns component T assigned to entity, throws if no component
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="e">Entity handle</param>
        /// <returns></returns>
        template<typename T>
        T& Get(Entity e);

        /// <summary>
        /// Returns ptr to component T assigned to entity, returns nullptr if no component
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="e">Entity handle</param>
        /// <returns></returns>
        template<typename T>
        T* TryGet(Entity e) noexcept;

        /// <summary>
        /// returns true if entity has component, otherwise false
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="e">Entity handle</param>
        /// <returns></returns>
        template<typename T>
        bool Has(Entity e) const noexcept;

        /// <summary>
        /// Register lambda function of type [](ECS::ArchetypeContext ctx, float dt, SharedDataRef _data) to run each frame on Update or Render.
        /// </summary>
        /// <typeparam name="...Ts"></typeparam>
        /// <param name="name">System name</param>
        /// <param name="fn">lambda</param>
        /// <param name="group">Update/Render</param>
        /// <returns>System builder for .Read(); .Write() modifications</returns>
        template<typename... Ts>
        SystemBuilder RegisterSystem(StringId name, SystemFn fn, SystemGroup group = SystemGroup::Update);

        /// <summary>
        /// Enable system
        /// </summary>
        /// <param name="name">System name</param>
        void EnableSystem(StringId name);
        /// <summary>
        /// Disable system
        /// </summary>
        /// <param name="name">System name</param>
        void DisableSystem(StringId name);

        /// <summary>
        /// Run system assigned to Update/Render
        /// </summary>
        /// <param name="group">Update/Render</param>
        /// <param name="dt">time step</param>
        void Run(SystemGroup group, float dt);

        /// <summary>
        /// returns a vector of all archetypes that have entities with at least components ... Ts
        /// </summary>
        /// <typeparam name="...Ts">components</typeparam>
        /// <returns></returns>
        template<typename... Ts>
        std::vector<ArchetypeContext> View()
        {
            ComponentMask mask = MakeMask<Ts...>();
            std::vector<ArchetypeContext> result;
            for (auto& [m, arch_ptr] : archetypes_)
            {
                if (!arch_ptr->HasComponents(mask))
                    continue;
                for (std::size_t i = 0; i < arch_ptr->chunks.size(); i++)
                    if (!arch_ptr->chunks[i].Empty())
                        result.push_back({ arch_ptr.get(), i, this });
            }
            return result;
        }
    };

    template<typename T>
    void World::Add(Entity e, T value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        assert(Alive(e));

        uint32_t eid = EntityId(e);
        auto& rec = records_[eid];
        ComponentId id = GetComponentId<T>();

        if (rec.arch->HasComponent(id))
        {
            rec.arch->Set<T>(rec.chunk_idx, rec.row, value);
            return;
        }

        Archetype& dst = GetOrCreateEdge(*rec.arch, id, sizeof(T), true);
        auto [dst_ci, dst_row] = rec.arch->MigrateEntityTo(rec.chunk_idx, rec.row, dst);
        dst.GetEntity(dst_ci, dst_row) = e;
        dst.Set<T>(dst_ci, dst_row, value);

        Entity moved = rec.arch->SwapRemove(rec.chunk_idx, rec.row);
        if (moved != e)
        {
            uint32_t moved_id = EntityId(moved);
            records_[moved_id].arch = rec.arch;
            records_[moved_id].chunk_idx = rec.chunk_idx;
            records_[moved_id].row = rec.row;
        }

        records_[eid] = { &dst, dst_ci, dst_row, records_[eid].generation };
    }

    template<typename T>
    void World::Remove(Entity e)
    {
        assert(Alive(e));
        uint32_t eid = EntityId(e);
        auto& rec = records_[eid];
        ComponentId id = GetComponentId<T>();
        if (!rec.arch->HasComponent(id)) return;

        Archetype& dst = GetOrCreateEdge(*rec.arch, id, 0, false);
        auto [dst_ci, dst_row] = rec.arch->MigrateEntityTo(rec.chunk_idx, rec.row, dst);
        dst.GetEntity(dst_ci, dst_row) = e;

        Entity moved = rec.arch->SwapRemove(rec.chunk_idx, rec.row);
        if (moved != e)
        {
            uint32_t moved_id = EntityId(moved);
            records_[moved_id].arch = rec.arch;
            records_[moved_id].chunk_idx = rec.chunk_idx;
            records_[moved_id].row = rec.row;
        }

        records_[eid] = { &dst, dst_ci, dst_row, records_[eid].generation };
    }

    template<typename T>
    T& World::Get(Entity e)
    {
        assert(Alive(e));
        uint32_t eid = EntityId(e);
        return records_[eid].arch->Get<T>(records_[eid].chunk_idx, records_[eid].row);
    }

    template<typename T>
    T* World::TryGet(Entity e) noexcept
    {
        if (!Alive(e)) return nullptr;
        uint32_t eid = EntityId(e);
        auto& rec = records_[eid];
        if (!rec.arch->HasComponent(GetComponentId<T>())) return nullptr;
        return &rec.arch->Get<T>(rec.chunk_idx, rec.row);
    }

    template<typename T>
    bool World::Has(Entity e) const noexcept
    {
        if (!Alive(e)) return false;
        uint32_t eid = EntityId(e);
        return records_[eid].arch->HasComponent(GetComponentId<T>());
    }

    template<typename... Ts>
    SystemBuilder World::RegisterSystem(StringId name, SystemFn fn, SystemGroup group)
    {
        auto& vec = (group == SystemGroup::Update) ? update_systems_ : render_systems_;
        vec.push_back({ name, MakeMask<Ts...>(), std::move(fn), true, ~ComponentMask(0), ~ComponentMask(0) });
        systems_dirty_ = true;
        return SystemBuilder(vec.back());
    }

    template<typename... Ts>
    std::vector<ArchetypeContext> ArchetypeContext::View()
    {
        assert(world && "ArchetypeContext has no world pointer");
        return world->View<Ts...>();
    }

}