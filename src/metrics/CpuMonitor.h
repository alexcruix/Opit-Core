// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include <cstdint>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif

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
    // Unidades de 100 ns, a escala do FILETIME do Windows. O gemeo Linux converte os
    // jiffies do /proc/stat para esta escala, para que a aritmetica de deltas seja uma so.
    uint64_t m_prevIdle = 0;
    uint64_t m_prevKernel = 0;
    uint64_t m_prevUser = 0;
    bool m_firstSample = true;
    std::mutex m_mutex;
};

} // namespace opitcore
