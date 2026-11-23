// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "FanProvider.h"

namespace opitcore {

class LaptopFanProvider : public IFanProvider {
public:
    LaptopFanProvider();
    ~LaptopFanProvider() override;

    std::string GetProviderName() const override { return "Laptop EC / ACPI Provider"; }
    bool Initialize() override;
    void Shutdown() override;

    std::vector<FanDeviceInfo> EnumerateDevices() override;
    FanResult<FanReading> ReadTelemetry(const std::string& deviceId) override;
    FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) override;
    FanResult<bool> RestoreDefaultControl(const std::string& deviceId) override;

private:
    bool m_isLaptop = false;
    bool m_initialized = false;
};

} // namespace opitcore
