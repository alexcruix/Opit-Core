// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include "FanProvider.h"

namespace opitcore {

class WindowsAcpiFanProvider : public IFanProvider {
public:
    WindowsAcpiFanProvider();
    ~WindowsAcpiFanProvider() override;

    std::string GetProviderName() const override { return "Windows ACPI / WMI Provider"; }
    bool Initialize() override;
    void Shutdown() override;

    std::vector<FanDeviceInfo> EnumerateDevices() override;
    FanResult<FanReading> ReadTelemetry(const std::string& deviceId) override;
    FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) override;
    FanResult<bool> RestoreDefaultControl(const std::string& deviceId) override;

private:
    bool m_initialized = false;
    bool m_acpiUnsupported = false;
    double QueryAcpiTemperature(bool& outHasSensor);
};

} // namespace opitcore
