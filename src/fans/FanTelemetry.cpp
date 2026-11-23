// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "FanTelemetry.h"
#include <chrono>

namespace opitcore {

FanTelemetry::FanTelemetry() = default;
FanTelemetry::~FanTelemetry() = default;

FanReading FanTelemetry::SampleDevice(std::shared_ptr<IFanProvider> provider, const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!provider) {
        FanReading r;
        r.timestampMs = nowMs;
        r.isStale = true;
        r.lastErrorMessage = "Provedor nulo ou indisponivel.";
        m_cachedReadings[deviceId] = r;
        return r;
    }

    auto res = provider->ReadTelemetry(deviceId);
    FanReading reading;
    if (res.IsSuccess()) {
        reading = res.value;
        reading.isStale = false;
    } else {
        // Se a leitura falhou, checar se tínhamos uma anterior recente
        auto it = m_cachedReadings.find(deviceId);
        if (it != m_cachedReadings.end() && (nowMs - it->second.timestampMs) < 10000) {
            reading = it->second;
            reading.isStale = true;
            reading.lastErrorMessage = res.message;
        } else {
            reading.timestampMs = nowMs;
            reading.isStale = true;
            reading.hasTemperature = false;
            reading.hasRpm = false;
            reading.lastErrorMessage = res.message;
        }
    }

    m_cachedReadings[deviceId] = reading;
    return reading;
}

FanReading FanTelemetry::GetLatestCached(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cachedReadings.find(deviceId);
    if (it != m_cachedReadings.end()) {
        uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        FanReading r = it->second;
        if (nowMs - r.timestampMs > 5000) {
            r.isStale = true;
        }
        return r;
    }
    FanReading empty;
    empty.isStale = true;
    return empty;
}

bool FanTelemetry::IsReadingStale(const std::string& deviceId, uint64_t maxAgeMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cachedReadings.find(deviceId);
    if (it == m_cachedReadings.end()) return true;

    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return (nowMs - it->second.timestampMs) > maxAgeMs;
}

} // namespace opitcore
