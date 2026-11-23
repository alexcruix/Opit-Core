// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "utils/Types.h"
#include <string>
#include <cstdint>
#include <vector>

namespace opitcore {

enum class MetricState {
    Valid,
    Unavailable,
    Stale,
    Inconsistent,
    Suspicious
};

inline const char* MetricStateToString(MetricState s) {
    switch (s) {
        case MetricState::Valid: return "Valid";
        case MetricState::Unavailable: return "Unavailable";
        case MetricState::Stale: return "Stale";
        case MetricState::Inconsistent: return "Inconsistent";
        case MetricState::Suspicious: return "Suspicious";
        default: return "Unknown";
    }
}

template <typename T>
struct ValidatedMetric {
    T value{};
    MetricState state = MetricState::Unavailable;
    DataOrigin origin = DataOrigin::Unknown;
    uint64_t timestampMs = 0;
    double uncertainty = 0.0; // Incerteza conhecida (ex: ±0.5%)
    std::string unit;
    std::string reason;

    bool IsUsable() const {
        return (state == MetricState::Valid || state == MetricState::Suspicious);
    }
};

struct ValidatedSystemTelemetry {
    ValidatedMetric<double> cpuUsage;           // % (0.0 - 100.0)
    ValidatedMetric<double> ramUsage;           // % (0.0 - 100.0)
    ValidatedMetric<uint64_t> ramUsedMB;        // MB
    ValidatedMetric<uint64_t> ramTotalMB;       // MB
    ValidatedMetric<double> cpuTemperatureC;    // °C
    ValidatedMetric<double> diskBusyPercent;    // % (0.0 - 100.0)
    ValidatedMetric<double> gpuUsagePercent;    // % (0.0 - 100.0)
    ValidatedMetric<double> frameTimeMs;        // ms/quadro medido pelo compositor DWM (indisponivel sem janela/DWM)
    ValidatedMetric<double> gpuPowerWatts;      // W medidos pelo driver da GPU (indisponivel sem sensor do fabricante)

    bool isThermalThrottling = false;
    bool isGameRunning = false;
    std::string runningGameName;
    std::string activePowerSchemeGuid;
    std::string activeProfileName;
    uint64_t sampleTimestampMs = 0;
};

class TelemetryValidator {
public:
    TelemetryValidator();

    // Validação e normalização de telemetria
    ValidatedSystemTelemetry Validate(const SystemMetricsSnapshot& rawSnapshot, uint32_t activeProcessId = 0);

private:
    uint64_t GetCurrentTimestampMs() const;
    double SampleProcessDiskActivity(uint32_t pid);

    uint64_t m_lastSampleTimeMs = 0;
    double m_lastCpuUsage = -1.0;
    double m_lastTempC = -1.0;
    uint64_t m_lastIoReadBytes = 0;
    uint64_t m_lastIoWriteBytes = 0;
    uint64_t m_lastIoTimeMs = 0;
};

} // namespace opitcore
