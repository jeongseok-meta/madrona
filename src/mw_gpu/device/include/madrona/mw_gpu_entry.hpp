#pragma once

#include <madrona/taskgraph.hpp>
#include <madrona/memory.hpp>
#include <madrona/mw_gpu/host_print.hpp>
#include <madrona/mw_gpu/tracing.hpp>

namespace madrona {
namespace mwGPU {
namespace entryKernels {

template <typename ContextT, typename WorldDataT, typename ConfigT,
          typename... InitTs>
__launch_bounds__(madrona::consts::numMegakernelThreads,
                  madrona::consts::numMegakernelBlocksPerSM)
__global__ void initECS(HostAllocInit alloc_init, void *print_channel,
                        void **exported_columns, void *cfg)
{
    HostAllocator *host_alloc = mwGPU::getHostAllocator();
    new (host_alloc) HostAllocator(alloc_init);

    auto host_print = (HostPrint *)GPUImplConsts::get().hostPrintAddr;
    new (host_print) HostPrint(print_channel);

    TmpAllocator &tmp_alloc = TmpAllocator::get();
    new (&tmp_alloc) TmpAllocator();

#ifdef MADRONA_TRACING
    new (&DeviceTracing::get()) DeviceTracing();
#endif

    StateManager *state_mgr = mwGPU::getStateManager();
    new (state_mgr) StateManager(0);

    ECSRegistry ecs_registry(*state_mgr, exported_columns);
    WorldDataT::registerTypes(ecs_registry, *(ConfigT *)cfg);
}

template <typename ContextT, typename WorldDataT, typename ConfigT,
          typename... InitTs>
__launch_bounds__(madrona::consts::numMegakernelThreads,
                  madrona::consts::numMegakernelBlocksPerSM)
__global__ void initWorlds(int32_t num_worlds,
                           const ConfigT *cfg,
                           InitTs * ...inits)
{
    int32_t world_idx = threadIdx.x + blockDim.x * blockIdx.x;

    if (world_idx >= num_worlds) {
        return;
    }

    WorldBase *world = TaskGraph::getWorld(world_idx);

    ContextT ctx = TaskGraph::makeContext<ContextT>(WorldID {
        world_idx,
    });

    new (world) WorldDataT(ctx, *cfg, inits[world_idx] ...);
}

template <typename ContextT, typename WorldDataT, typename ConfigT,
          typename... InitTs>
__launch_bounds__(madrona::consts::numMegakernelThreads,
                  madrona::consts::numMegakernelBlocksPerSM)
__global__ void initTasks(void *cfg)
{
    TaskGraph::Builder builder(1024, 1024 * 2, 1024 * 5);
    WorldDataT::setupTasks(builder, *(ConfigT *)cfg);

    builder.build((TaskGraph *)mwGPU::GPUImplConsts::get().taskGraph);
}

}

template <auto init_ecs, auto init_worlds, auto init_tasks>
struct MWGPUEntryInstantiate {};

template <typename ContextT, typename WorldDataT, typename ConfigT,
          typename... InitTs>
struct alignas(16) MWGPUEntry : MWGPUEntryInstantiate<
    entryKernels::initECS<ContextT, WorldDataT, ConfigT, InitTs...>,
    entryKernels::initWorlds<ContextT, WorldDataT, ConfigT, InitTs...>,
    entryKernels::initTasks<ContextT, WorldDataT, ConfigT, InitTs...>>
{};

}
}

// This macro forces MWGPUEntry to be instantiated, which in turn instantiates
// the entryKernels::* __global__ entry points. static_assert with a trivially
// true check leaves no side effects in the scope where this macro is called.
#define MADRONA_BUILD_MWGPU_ENTRY(ContextT, WorldT, ConfigT, ...) \
    static_assert(alignof(::madrona::mwGPU::MWGPUEntry<ContextT, WorldT, \
        ConfigT, __VA_ARGS__>) == 16);
