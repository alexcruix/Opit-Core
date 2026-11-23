// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "FanTypes.h"
#include "FanProvider.h"
#include <unordered_map>
#include <mutex>

namespace opitcore {

class FanTelemetry {
public:
    FanTelemetry();
    ~FanTelemetry();

    FanReading SampleDevice(std::shared_ptr<IFanProvider> provider, const std::string& deviceId);
    FanReading GetLatestCached(const std::string& deviceId);
    bool IsReadingStale(const std::string& deviceId, uint64_t maxAgeMs = 5000);

private:
    std::unordered_map<std::string, FanReading> m_cachedReadings;
    std::mutex m_mutex;
};

} // namespace opitcore
