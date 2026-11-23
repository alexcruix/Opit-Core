// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "FanControlManager.h"
#ifdef _WIN32
#include "WindowsAcpiFanProvider.h"
#include "NvidiaNvapiFanProvider.h"
#include "AmdAdlFanProvider.h"
#include "IntelGpuFanProvider.h"
#include "LaptopFanProvider.h"
#include "LibreHardwareMonitorFanProvider.h"
#else
#include "HwmonFanProvider.h"
#endif
#include "MockFanProvider.h"
#include "core/DatabaseManager.h"
#include "core/Logger.h"
#include "core/ModuleAdvancedSettings.h"
#include <sstream>

namespace opitcore {

FanControlManager& FanControlManager::Instance() {
    static FanControlManager instance;
    return instance;
}

FanControlManager::FanControlManager() = default;

FanControlManager::~FanControlManager() {
    Shutdown();
}

bool FanControlManager::Initialize(bool enableMockForTesting) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    OPIT_LOG_INFO("FanControlManager: Inicializando arquitetura modular de ventoinhas...");

    // Limiares termicos da salvaguarda seguem a configuracao da usuaria, nao valores fixos.
    const auto& thCfg = ModuleAdvancedSettings::Instance().thermal;
    m_safety.SetThermalThresholds(thCfg.criticalLimitC, thCfg.alertLimitC);

    // Registro de Adaptadores de Hardware
#ifdef _WIN32
    m_discovery.RegisterProvider(std::make_shared<WindowsAcpiFanProvider>());
    m_discovery.RegisterProvider(std::make_shared<NvidiaNvapiFanProvider>());
    m_discovery.RegisterProvider(std::make_shared<AmdAdlFanProvider>());
    m_discovery.RegisterProvider(std::make_shared<IntelGpuFanProvider>());
    m_discovery.RegisterProvider(std::make_shared<LaptopFanProvider>());
    m_discovery.RegisterProvider(std::make_shared<LibreHardwareMonitorFanProvider>());
#else
    // O kernel expõe por sysfs os mesmos sensores e o mesmo comando de duty que a ACPI/NVAPI/ADL
    // dão no Windows: um provider chega para todos os chips com driver hwmon.
    m_discovery.RegisterProvider(std::make_shared<HwmonFanProvider>());
#endif

    if (enableMockForTesting) {
        m_discovery.RegisterProvider(std::make_shared<MockFanProvider>());
    }

    m_discovery.InitializeAll();
    m_devices = m_discovery.DiscoverAllDevices();

    m_diagnostics.Log("INFO", "system", "Descoberta de ventoinhas concluida. Dispositivos identificados: " +
                      std::to_string(m_devices.size()));

    m_initialized = true;
    LoadConfigurations();
    return true;
}

void FanControlManager::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    for (const auto& dev : m_devices) {
        if (dev.activeMode != FanControlMode::BiosFirmwareAuto) {
            auto provider = m_discovery.GetProviderForDevice(dev.id);
            if (provider) {
                provider->RestoreDefaultControl(dev.id);
            }
        }
    }

    m_discovery.ShutdownAll();
    m_devices.clear();
    m_initialized = false;
    OPIT_LOG_INFO("FanControlManager: Finalizado com sucesso.");
}

std::vector<FanDeviceInfo> FanControlManager::GetDevices() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_devices;
}

bool FanControlManager::GetDeviceInfo(const std::string& deviceId, FanDeviceInfo& outInfo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& dev : m_devices) {
        if (dev.id == deviceId) {
            outInfo = dev;
            return true;
        }
    }
    return false;
}

bool FanControlManager::SetDeviceMode(const std::string& deviceId, FanControlMode mode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& dev : m_devices) {
        if (dev.id == deviceId) {
            if (mode != FanControlMode::BiosFirmwareAuto && !dev.capabilities.canControlSpeed) {
                m_diagnostics.Log("WARN", deviceId, "Tentativa de ativar controle manual/curva em dispositivo sem suporte de controle.");
                return false;
            }

            dev.activeMode = mode;
            m_curveController.ResetDeviceState(deviceId);

            if (mode == FanControlMode::BiosFirmwareAuto) {
                auto provider = m_discovery.GetProviderForDevice(deviceId);
                if (provider) {
                    provider->RestoreDefaultControl(deviceId);
                }
            }

            m_diagnostics.Log("INFO", deviceId, "Modo de operacao alterado para: " + std::string(FanControlModeToString(mode)));
            SaveConfigurations();
            return true;
        }
    }
    return false;
}

bool FanControlManager::SetManualSpeed(const std::string& deviceId, uint32_t speedPercent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& dev : m_devices) {
        if (dev.id == deviceId) {
            if (!dev.capabilities.canControlSpeed) {
                m_diagnostics.Log("WARN", deviceId, "Controle manual rejeitado: dispositivo nao suporta controle de velocidade.");
                return false;
            }

            dev.manualTargetSpeedPercent = speedPercent;
            dev.activeMode = FanControlMode::ManualFixed;

            // Avaliação de Segurança
            uint32_t safeSpeed = speedPercent;
            std::string reason;
            auto action = m_safety.EvaluateSafety(dev, dev.latestReading, speedPercent, safeSpeed, reason);

            auto provider = m_discovery.GetProviderForDevice(deviceId);
            if (!provider) return false;

            if (action == FanSafetyAction::FallbackToFirmware) {
                provider->RestoreDefaultControl(deviceId);
                m_diagnostics.Log("WARN", deviceId, "Seguranca acionou retorno ao firmware: " + reason);
                return false;
            }

            auto res = provider->SetSpeedPercent(deviceId, safeSpeed);
            if (res.IsSuccess()) {
                m_diagnostics.Log("INFO", deviceId, "Velocidade manual aplicada: " + std::to_string(safeSpeed) + "% (" + reason + ")");
            } else {
                m_diagnostics.Log("ERROR", deviceId, "Falha ao aplicar velocidade: " + res.message);
            }

            SaveConfigurations();
            return res.IsSuccess();
        }
    }
    return false;
}

bool FanControlManager::SetDeviceCurve(const std::string& deviceId, const FanCurveConfig& curve) {
    std::string err;
    if (!FanCurveController::ValidateCurve(curve, err)) {
        m_diagnostics.Log("ERROR", deviceId, "Curva rejeitada por erro de validacao: " + err);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& dev : m_devices) {
        if (dev.id == deviceId) {
            dev.curveConfig = curve;
            dev.curveConfig.Validate();
            m_curveController.ResetDeviceState(deviceId);
            m_diagnostics.Log("INFO", deviceId, "Curva personalizada atualizada com sucesso.");
            SaveConfigurations();
            return true;
        }
    }
    return false;
}

bool FanControlManager::RestoreFirmwareControl(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& dev : m_devices) {
        if (dev.id == deviceId) {
            dev.activeMode = FanControlMode::BiosFirmwareAuto;
            auto provider = m_discovery.GetProviderForDevice(deviceId);
            if (provider) {
                provider->RestoreDefaultControl(deviceId);
            }
            m_diagnostics.Log("INFO", deviceId, "Controle de ventilacao restaurado para o BIOS / Firmware.");
            SaveConfigurations();
            return true;
        }
    }
    return false;
}

void FanControlManager::RefreshTelemetryOnly() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    for (auto& dev : m_devices) {
        auto provider = m_discovery.GetProviderForDevice(dev.id);
        if (!provider) continue;
        FanReading reading = m_telemetry.SampleDevice(provider, dev.id);
        dev.latestReading = reading;
        if (reading.hasTemperature) {
            m_safety.RecordReadingSuccess(dev.id);
        } else {
            m_safety.RecordReadingFailure(dev.id);
        }
    }
}

bool FanControlManager::QueryBestAvailableTemperature(double& outTempC, std::string& outSource, uint32_t* outRpm, bool* outHasRpm) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (outHasRpm) *outHasRpm = false;
    if (outRpm) *outRpm = 0;
    if (!m_initialized) return false;

    bool foundTemp = false;
    // Priorizar temperatura da CPU/Placa-Mãe se válida (> 0), senão LibreHardwareMonitor / GPU dedicada
    for (const auto& dev : m_devices) {
        if (dev.deviceType == FanDeviceType::VirtualMock) continue;
        const auto& r = dev.latestReading;
        if (r.hasTemperature && r.temperatureC > 0.5 && r.temperatureC < 130.0) {
            if (!foundTemp || (dev.id == "mb:acpi:cpu")) {
                outTempC = r.temperatureC;
                outSource = r.sensorSource.empty() ? dev.name : (dev.name + " [" + r.sensorSource + "]");
                foundTemp = true;
            }
        }
        if (r.hasRpm && outHasRpm && outRpm) {
            if (!(*outHasRpm) || (*outRpm == 0 && r.rpm > 0)) {
                *outHasRpm = true;
                *outRpm = r.rpm;
            }
        }
    }
    return foundTemp;
}

void FanControlManager::ProcessPeriodicTick() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    for (auto& dev : m_devices) {
        auto provider = m_discovery.GetProviderForDevice(dev.id);
        if (!provider) continue;

        // 1. Amostrar Telemetria
        FanReading reading = m_telemetry.SampleDevice(provider, dev.id);
        dev.latestReading = reading;

        if (reading.hasTemperature) {
            m_safety.RecordReadingSuccess(dev.id);
        } else {
            m_safety.RecordReadingFailure(dev.id);
        }

        // 2. Executar Lógica de Modo Ativo
        if (dev.activeMode == FanControlMode::CustomCurve && dev.capabilities.canControlSpeed) {
            if (!reading.hasTemperature) {
                // Sem leitura de temperatura: aplicar salvaguarda
                uint32_t safeSpeed = 100;
                std::string reason;
                auto action = m_safety.EvaluateSafety(dev, reading, 100, safeSpeed, reason);
                if (action == FanSafetyAction::FallbackToFirmware) {
                    provider->RestoreDefaultControl(dev.id);
                } else {
                    provider->SetSpeedPercent(dev.id, safeSpeed);
                }
                continue;
            }

            uint32_t targetSpeed = m_curveController.EvaluateSpeed(dev.id, dev.curveConfig, reading.temperatureC);
            uint32_t safeSpeed = targetSpeed;
            std::string reason;
            auto action = m_safety.EvaluateSafety(dev, reading, targetSpeed, safeSpeed, reason);

            if (action == FanSafetyAction::FallbackToFirmware) {
                provider->RestoreDefaultControl(dev.id);
            } else {
                provider->SetSpeedPercent(dev.id, safeSpeed);
            }
        } else if (dev.activeMode == FanControlMode::ManualFixed && dev.capabilities.canControlSpeed) {
            uint32_t safeSpeed = dev.manualTargetSpeedPercent;
            std::string reason;
            auto action = m_safety.EvaluateSafety(dev, reading, dev.manualTargetSpeedPercent, safeSpeed, reason);

            if (action == FanSafetyAction::ForceEmergencyFullSpeed) {
                provider->SetSpeedPercent(dev.id, 100);
            } else if (action == FanSafetyAction::FallbackToFirmware) {
                provider->RestoreDefaultControl(dev.id);
            } else {
                // ClampToSafeMinimum/Allow: o valor seguro calculado (que pode ter sido rebaixado
                // pelo piso de seguranca) e que vai para o hardware, nao o alvo manual bruto.
                provider->SetSpeedPercent(dev.id, safeSpeed);
            }
        }
    }
}

bool FanControlManager::SaveConfigurations() {
    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return false;

    for (const auto& dev : m_devices) {
        std::string prefix = "fan_cfg_" + dev.id + "_";
        db.SetSetting(prefix + "mode", std::to_string(static_cast<int>(dev.activeMode)));
        db.SetSetting(prefix + "manual_speed", std::to_string(dev.manualTargetSpeedPercent));
        db.SetSetting(prefix + "hysteresis", std::to_string(dev.curveConfig.hysteresisC));
        db.SetSetting(prefix + "emergency_temp", std::to_string(dev.curveConfig.emergencyTempC));

        // Serializar pontos da curva: "35:30;50:45;65:65;75:85;85:100"
        std::string ptsStr;
        for (size_t i = 0; i < dev.curveConfig.points.size(); ++i) {
            ptsStr += std::to_string(static_cast<int>(dev.curveConfig.points[i].tempC)) + ":" +
                      std::to_string(dev.curveConfig.points[i].speedPercent);
            if (i + 1 < dev.curveConfig.points.size()) ptsStr += ";";
        }
        db.SetSetting(prefix + "curve_points", ptsStr);
    }
    return true;
}

bool FanControlManager::LoadConfigurations() {
    auto& db = DatabaseManager::Instance();
    if (!db.IsOpen()) return false;

    for (auto& dev : m_devices) {
        std::string prefix = "fan_cfg_" + dev.id + "_";
        std::string modeStr = db.GetSetting(prefix + "mode", "");
        if (!modeStr.empty()) {
            try {
                int modeInt = std::stoi(modeStr);
                dev.activeMode = static_cast<FanControlMode>(modeInt);
            } catch (...) {}
        }

        std::string speedStr = db.GetSetting(prefix + "manual_speed", "");
        if (!speedStr.empty()) {
            try {
                dev.manualTargetSpeedPercent = static_cast<uint32_t>(std::stoul(speedStr));
            } catch (...) {}
        }

        std::string hystStr = db.GetSetting(prefix + "hysteresis", "");
        if (!hystStr.empty()) {
            try {
                dev.curveConfig.hysteresisC = std::stoi(hystStr);
            } catch (...) {}
        }

        std::string emergStr = db.GetSetting(prefix + "emergency_temp", "");
        if (!emergStr.empty()) {
            try {
                dev.curveConfig.emergencyTempC = std::stod(emergStr);
            } catch (...) {}
        }

        std::string ptsStr = db.GetSetting(prefix + "curve_points", "");
        if (!ptsStr.empty()) {
            std::vector<FanCurvePoint> loadedPts;
            std::stringstream ss(ptsStr);
            std::string item;
            while (std::getline(ss, item, ';')) {
                size_t colon = item.find(':');
                if (colon != std::string::npos) {
                    try {
                        double t = std::stod(item.substr(0, colon));
                        uint32_t s = static_cast<uint32_t>(std::stoul(item.substr(colon + 1)));
                        loadedPts.push_back({t, s});
                    } catch (...) {}
                }
            }
            if (!loadedPts.empty()) {
                dev.curveConfig.points = loadedPts;
                dev.curveConfig.Validate();
            }
        }
    }
    return true;
}

} // namespace opitcore
