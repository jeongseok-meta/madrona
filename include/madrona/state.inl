#pragma once

#include <madrona/utils.hpp>

#include <array>

namespace madrona {

template <typename ComponentT>
ComponentID StateManager::registerComponent()
{
    TypeTracker::registerType<ComponentT>(&next_component_id_);

    uint32_t id = TypeTracker::typeID<ComponentT>();

    saveComponentInfo(id, std::alignment_of_v<ComponentT>,
                      sizeof(ComponentT));

    return ComponentID {
        id,
    };
}

template <typename ArchetypeT>
ArchetypeID StateManager::registerArchetype()
{
    TypeTracker::registerType<ArchetypeT>(&next_archetype_id_);

    using Delegator = utils::PackDelegator<ArchetypeT>;

    auto archetype_components = Delegator::call([]<typename... Args>() {
        static_assert(std::is_same_v<ArchetypeT, Archetype<Args...>>);
        uint32_t column_idx = 0;
        auto registerColumnIndex =
                [&column_idx]<typename ComponentT>() {
            using LookupT = typename ArchetypeRef<ArchetypeT>::
                template ComponentLookup<ComponentT>;

            TypeTracker::registerType<LookupT>(&column_idx);
        };

        ( registerColumnIndex.template operator()<Args>(), ... );

        std::array archetype_components {
            ComponentID { TypeTracker::typeID<Args>() }
            ...
        };

        return archetype_components;
    });
    
    uint32_t id = TypeTracker::typeID<ArchetypeT>();

    saveArchetypeInfo(id,
        Span(archetype_components.data(), archetype_components.size()));

    return ArchetypeID {
        id,
    };
}

template <typename ComponentT>
ComponentID StateManager::componentID() const
{
    return ComponentID {
        TypeTracker::typeID<ComponentT>(),
    };
}

template <typename ArchetypeT>
ArchetypeID StateManager::archetypeID() const
{
    return ArchetypeID {
        TypeTracker::typeID<ArchetypeT>(),
    };
}

template <typename ComponentT>
ResultRef<ComponentT> StateManager::get(Entity entity)
{
    uint32_t archetype_idx = entity.archetype;

    ArchetypeInfo &archetype = *archetype_infos_[archetype_idx];

    auto col_idx = archetype.columnLookup.lookup(componentID<ComponentT>().id);

    if (!col_idx.has_value()) {
        return ResultRef<ComponentT>(nullptr);
    }

    auto loc = archetype.tbl.getLoc(entity);
    if (!loc.valid()) {
        return ResultRef<ComponentT>(nullptr);
    }

    return ResultRef<ComponentT>(
        (ComponentT *)archetype.tbl.getValue(*col_idx, loc));
}

template <typename... ComponentTs>
Query<ComponentTs...> StateManager::query()
{
    std::array component_ids {
        componentID<ComponentTs>()
        ...
    };

    const uint32_t *query_indices;
    uint32_t num_archetypes = 
        makeQuery(component_ids.data(), component_ids.size(), &query_indices);

    return Query<ComponentTs...>(query_indices, num_archetypes);
}

template <typename ArchetypeT>
ArchetypeRef<ArchetypeT> StateManager::archetype()
{
    auto archetype_id = archetypeID<ArchetypeT>();
    return ArchetypeRef<ArchetypeT>(&archetype_infos_[archetype_id.id]->tbl);
}

template <typename ArchetypeT, typename... Args>
Entity StateManager::makeEntity(Args && ...args)
{
    ArchetypeID archetype_id = archetypeID<ArchetypeT>();

    ArchetypeInfo &archetype = *archetype_infos_[archetype_id.id];

    assert(sizeof...(Args) == archetype.numComponents &&
           "Trying to construct entity with wrong number of arguments");


    Entity new_row = archetype.tbl.addRow();

    auto tbl_loc = archetype.tbl.getLoc(new_row);

    int column_idx = 0;

    auto constructNextComponent = [this, &column_idx, &archetype, &tbl_loc](
            auto &&arg) {
        using ComponentT = std::remove_reference_t<decltype(arg)>;

        assert(componentID<ComponentT>().id ==
               archetype_components_[archetype.componentOffset +
                   column_idx].id);

        new (archetype.tbl.getValue(column_idx, tbl_loc)) 
            ComponentT(std::forward<ComponentT>(arg));

        column_idx++;
    };

    ( constructNextComponent(std::forward<Args>(args)), ... );

    return new_row;
}

void StateManager::destroyEntity(Entity e)
{
    ArchetypeInfo &archetype = *archetype_infos_[e.archetype];
    archetype.tbl.removeRow(e);
}

}
