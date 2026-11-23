// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "SecurityPerformanceMonitor.h"

namespace opitcore {
namespace security {

SecurityPerformanceMonitor::SecurityPerformanceMonitor() = default;

void SecurityPerformanceMonitor::StartStartupTimer() {
    m_startTime = std::chrono::high_resolution_clock::now();
    m_timingActive = true;
}

void SecurityPerformanceMonitor::StopStartupTimer() {
    if (!m_timingActive) return;
    auto endTime = std::chrono::high_resolution_clock::now();
    m_elapsedMs = std::chrono::duration<double, std::milli>(endTime - m_startTime).count();
    m_timingActive = false;
}

void SecurityPerformanceMonitor::RecordCheck(bool cacheHit, bool hashCalculated) {
    m_filesChecked++;
    if (cacheHit) m_cacheHits++;
    if (hashCalculated) m_hashCalculations++;
}

SecurityMetrics SecurityPerformanceMonitor::GetMetrics() const {
    SecurityMetrics m;
    m.startupCheckTimeMs = m_elapsedMs;
    m.totalFilesChecked = m_filesChecked;
    m.cacheHits = m_cacheHits;
    m.hashCalculations = m_hashCalculations;
    m.isSecure = true;
    return m;
}

} // namespace security
} // namespace opitcore
