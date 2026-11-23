// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

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
