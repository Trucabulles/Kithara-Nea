#pragma once
#include <algorithm>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

namespace kn {
inline int analysisThreadsForCpus(unsigned available) {
    return static_cast<int>(std::min(12u, std::max(1u, available / 2)));
}

inline unsigned availableLogicalCpus() {
#if defined(_WIN32)
    // Explicit process CPU sets take precedence over the unrestricted group count.
    ULONG selectedCount = 0;
    GetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0, &selectedCount);
    std::vector<ULONG> selected(selectedCount);
    if (selectedCount && !GetProcessDefaultCpuSets(GetCurrentProcess(), selected.data(), selectedCount,
                                                 &selectedCount))
        selected.clear();
    DWORD_PTR processMask = 0, systemMask = 0;
    const bool hasMask = GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) != 0;
    GROUP_AFFINITY currentGroup{};
    GetThreadGroupAffinity(GetCurrentThread(), &currentGroup);
    USHORT groupCount = 0;
    GetProcessGroupAffinity(GetCurrentProcess(), &groupCount, nullptr);
    std::vector<USHORT> groups(groupCount);
    if (groupCount && !GetProcessGroupAffinity(GetCurrentProcess(), &groupCount, groups.data()))
        groups.clear();
    ULONG bytes = 0;
    GetSystemCpuSetInformation(nullptr, 0, &bytes, GetCurrentProcess(), 0);
    std::vector<unsigned char> storage(bytes);
    if (bytes && GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(storage.data()),
                                            bytes, &bytes, GetCurrentProcess(), 0)) {
        unsigned count = 0;
        for (ULONG offset = 0; offset < bytes;) {
            auto &entry = *reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(storage.data() + offset);
            if (!entry.Size || entry.Size > bytes - offset)
                break;
            if (entry.Type == CpuSetInformation) {
                const auto &cpu = entry.CpuSet;
                const bool chosen = selected.empty() ||
                    std::find(selected.begin(), selected.end(), cpu.Id) != selected.end();
                const bool inGroup = groups.empty() ||
                    std::find(groups.begin(), groups.end(), cpu.Group) != groups.end();
                const bool affinity = inGroup && (!hasMask || !processMask || cpu.Group != currentGroup.Group ||
                    (processMask & (DWORD_PTR(1) << cpu.LogicalProcessorIndex)));
                if (chosen && affinity && (!cpu.Allocated || cpu.AllocatedToTargetProcess))
                    ++count;
            }
            offset += entry.Size;
        }
        if (count)
            return count;
    }
    if (hasMask && processMask) {
        unsigned count = 0;
        for (; processMask; processMask &= processMask - 1)
            ++count;
        return count;
    }
#elif defined(__linux__)
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0 && CPU_COUNT(&affinity))
        return static_cast<unsigned>(CPU_COUNT(&affinity));
#endif
    return std::max(1u, std::thread::hardware_concurrency());
}

inline int analysisCpuThreads() {
    return analysisThreadsForCpus(availableLogicalCpus());
}
} // namespace kn
