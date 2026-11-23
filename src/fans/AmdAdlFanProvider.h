// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "FanProvider.h"
#include <windows.h>
#include <vector>

namespace opitcore {

class AmdAdlFanProvider : public IFanProvider {
public:
    AmdAdlFanProvider();
    ~AmdAdlFanProvider() override;

    std::string GetProviderName() const override { return "AMD ADL Fan Provider"; }
    bool Initialize() override;
    void Shutdown() override;

    std::vector<FanDeviceInfo> EnumerateDevices() override;
    FanResult<FanReading> ReadTelemetry(const std::string& deviceId) override;
    FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) override;
    FanResult<bool> RestoreDefaultControl(const std::string& deviceId) override;

private:
    HMODULE m_hAdlDll = nullptr;
    bool m_initialized = false;
    std::vector<int> m_adapterIndices;
    std::vector<std::string> m_adapterNames;
};

} // namespace opitcore
