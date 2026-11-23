// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once

#include <chrono>
#include <cstdint>
#include "SecurityTypes.h"

namespace opitcore {
namespace security {

class SecurityPerformanceMonitor {
public:
    SecurityPerformanceMonitor();
    ~SecurityPerformanceMonitor() = default;

    void StartStartupTimer();
    void StopStartupTimer();

    void RecordCheck(bool cacheHit, bool hashCalculated);
    SecurityMetrics GetMetrics() const;

private:
    std::chrono::high_resolution_clock::time_point m_startTime;
    double m_elapsedMs{0.0};
    uint32_t m_filesChecked{0};
    uint32_t m_cacheHits{0};
    uint32_t m_hashCalculations{0};
    bool m_timingActive{false};
};

} // namespace security
} // namespace opitcore
