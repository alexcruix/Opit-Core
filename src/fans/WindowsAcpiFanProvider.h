// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

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
