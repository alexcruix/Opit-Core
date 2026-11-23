// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "FanProvider.h"
#include <windows.h>
#include <vector>
#include <string>

namespace opitcore {

struct IntelFanHandleInfo {
    std::string deviceId;
    std::string name;
    void* hAdapter = nullptr;
    void* hFan = nullptr;
    void* hTempSensor = nullptr;
    bool canControl = false;
    bool canReadRpm = false;
    bool canReadTemp = false;
    int32_t maxRpm = -1;
    uint32_t supportedUnits = 0;
};

class IntelGpuFanProvider : public IFanProvider {
public:
    IntelGpuFanProvider();
    ~IntelGpuFanProvider() override;

    std::string GetProviderName() const override { return "Intel Graphics Control Library (IGCL)"; }
    bool Initialize() override;
    void Shutdown() override;

    std::vector<FanDeviceInfo> EnumerateDevices() override;
    FanResult<FanReading> ReadTelemetry(const std::string& deviceId) override;
    FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) override;
    FanResult<bool> RestoreDefaultControl(const std::string& deviceId) override;

private:
    bool m_initialized = false;
    HMODULE m_hControlLib = nullptr;
    void* m_hApiHandle = nullptr;
    std::vector<IntelFanHandleInfo> m_fans;
    std::vector<std::string> m_fallbackIntelGpuNames;
};

} // namespace opitcore
