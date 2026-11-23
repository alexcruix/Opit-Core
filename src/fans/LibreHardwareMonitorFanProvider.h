// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once

#include "FanProvider.h"
#include "lhm_bridge/OpitLhmBridge.h"

#include <string>
#include <vector>

namespace opitcore {

// Reads LibreHardwareMonitor sensors through opitcore_lhm_bridge.dll, which is
// loaded at runtime. If the DLL, LibreHardwareMonitorLib or the .NET Framework
// is missing, Initialize() fails and the provider stays unavailable.
class LibreHardwareMonitorFanProvider : public IFanProvider {
public:
    LibreHardwareMonitorFanProvider();
    ~LibreHardwareMonitorFanProvider() override;

    // IFanProvider interface
    std::string GetProviderName() const override;
    bool Initialize() override;
    void Shutdown() override;
    std::vector<FanDeviceInfo> EnumerateDevices() override;
    FanResult<FanReading> ReadTelemetry(const std::string& deviceId) override;
    FanResult<bool> SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) override;
    FanResult<bool> RestoreDefaultControl(const std::string& deviceId) override;

private:
    bool LoadBridge();
    void UnloadBridge();

    bool m_initialized = false;
    // Synthetic device representing LHM sensors
    const std::string kDeviceId = "LHM_0";
    const std::string kDeviceName = "LibreHardwareMonitor";

    // Runtime-loaded bridge (HMODULE kept opaque to avoid <windows.h> here).
    void* m_bridge = nullptr;
    PFN_OpitLhm_Init m_init = nullptr;
    PFN_OpitLhm_Refresh m_refresh = nullptr;
    PFN_OpitLhm_GetCount m_getTempCount = nullptr;
    PFN_OpitLhm_GetCount m_getFanCount = nullptr;
    PFN_OpitLhm_GetTemperature m_getTemperature = nullptr;
    PFN_OpitLhm_GetRpm m_getRpm = nullptr;
    PFN_OpitLhm_GetTemperatureInfo m_getTempInfo = nullptr;
    PFN_OpitLhm_GetFanInfo m_getFanInfo = nullptr;
    PFN_OpitLhm_SetFanControl m_setFanControl = nullptr;
    PFN_OpitLhm_RestoreFanControl m_restoreFanControl = nullptr;
    PFN_OpitLhm_Shutdown m_shutdown = nullptr;
};

} // namespace opitcore