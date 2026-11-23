// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "FanTypes.h"
#include <vector>
#include <string>
#include <memory>

namespace opitcore {

class IFanProvider {
public:
    virtual ~IFanProvider() = default;

    virtual std::string GetProviderName() const = 0;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual std::vector<FanDeviceInfo> EnumerateDevices() = 0;
    virtual FanResult<FanReading> ReadTelemetry(const std::string& deviceId) = 0;
    virtual FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) = 0;
    virtual FanResult<bool> RestoreDefaultControl(const std::string& deviceId) = 0;
};

} // namespace opitcore
