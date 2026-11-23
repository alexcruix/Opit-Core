// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once
#include "FanTypes.h"
#include "FanDiscovery.h"
#include "FanTelemetry.h"
#include "FanCurveController.h"
#include "FanSafetyController.h"
#include "FanDiagnostics.h"
#include <vector>
#include <memory>
#include <mutex>

namespace opitcore {

class FanControlManager {
public:
    static FanControlManager& Instance();

    bool Initialize(bool enableMockForTesting = false);
    void Shutdown();

    // Descoberta e Consulta
    std::vector<FanDeviceInfo> GetDevices();
    bool GetDeviceInfo(const std::string& deviceId, FanDeviceInfo& outInfo);

    // Controle
    bool SetDeviceMode(const std::string& deviceId, FanControlMode mode);
    bool SetManualSpeed(const std::string& deviceId, uint32_t speedPercent);
    bool SetDeviceCurve(const std::string& deviceId, const FanCurveConfig& curve);
    bool RestoreFirmwareControl(const std::string& deviceId);

    // Execução Periódica do Orquestrador
    void ProcessPeriodicTick();
    void RefreshTelemetryOnly();
    bool QueryBestAvailableTemperature(double& outTempC, std::string& outSource, uint32_t* outRpm = nullptr, bool* outHasRpm = nullptr);

    // Persistência com SQLite
    bool LoadConfigurations();
    bool SaveConfigurations();

    // Diagnóstico
    FanDiagnostics& GetDiagnostics() { return m_diagnostics; }
    FanDiscovery& GetDiscovery() { return m_discovery; }
    FanSafetyController& GetSafety() { return m_safety; }

private:
    FanControlManager();
    ~FanControlManager();

    FanDiscovery m_discovery;
    FanTelemetry m_telemetry;
    FanCurveController m_curveController;
    FanSafetyController m_safety;
    FanDiagnostics m_diagnostics;

    std::vector<FanDeviceInfo> m_devices;
    std::mutex m_mutex;
    bool m_initialized = false;
};

} // namespace opitcore
