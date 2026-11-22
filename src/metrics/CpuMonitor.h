#pragma once
#include <cstdint>
#include <mutex>
#include <windows.h>

namespace opitcore {

struct CpuMetrics {
    double totalUsagePercent = 0.0;
};

class CpuMonitor {
public:
    CpuMonitor();
    bool Initialize();
    CpuMetrics Sample();

private:
    FILETIME m_prevIdle{};
    FILETIME m_prevKernel{};
    FILETIME m_prevUser{};
    bool m_firstSample = true;
    std::mutex m_mutex;
};

} // namespace opitcore
