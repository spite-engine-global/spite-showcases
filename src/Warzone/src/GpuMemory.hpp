#pragma once
#include "engine/GpuMemoryService.hpp"

namespace warzone
{
    struct GpuMemory
    {
        uint64_t totalBytes = 0, budgetBytes = 0, usedBytes = 0;
        bool valid = false;
    };
    inline spite::GpuMemoryService gpuMemory;
    inline GpuMemory queryGpuMemory()
    {
        if (!gpuMemory.budget) return {};
        const auto budget = gpuMemory.budget();
        return {0, budget.budgetBytes, budget.usedBytes, budget.budgetBytes != 0};
    }
}
