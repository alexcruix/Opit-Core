#include "LibreHardwareMonitorFanProvider.h"
#include "core/Logger.h"
#include "security/OpitSecurityManager.h"
#include "utils/StringUtils.h"
#include <chrono>
#include <windows.h>

namespace opitcore {

LibreHardwareMonitorFanProvider::LibreHardwareMonitorFanProvider() = default;
LibreHardwareMonitorFanProvider::~LibreHardwareMonitorFanProvider() {
    if (m_initialized) {
        Shutdown();
    }
    UnloadBridge();
}

std::string LibreHardwareMonitorFanProvider::GetProviderName() const {
    return "LibreHardwareMonitor";
}

bool LibreHardwareMonitorFanProvider::LoadBridge() {
    if (m_bridge) return true;

    // Carregamento defensivo seguro via OpitSecurityManager
    // Valida o manifesto, assinatura digital RSA, integridade criptografica SHA-256 e protege contra TOCTOU
    security::IntegrityStatus secStatus = security::IntegrityStatus::Valid;
    HMODULE mod = security::OpitSecurityManager::Instance().LoadVerifiedModule(
        OPIT_LHM_BRIDGE_DLL_NAME, secStatus);

    if (!mod || secStatus != security::IntegrityStatus::Valid) {
        OPIT_LOG_ERROR("LibreHardwareMonitorFanProvider: integridade de " +
                       StringUtils::ToUtf8(OPIT_LHM_BRIDGE_DLL_NAME) +
                       " violada ou nao autorizada (Status: " +
                       security::IntegrityStatusToString(secStatus) + "). Bloqueando carregamento da bridge.");
        m_bridge = nullptr;
        return false;
    }

    auto getAbi = reinterpret_cast<PFN_OpitLhm_GetAbiVersion>(GetProcAddress(mod, "OpitLhm_GetAbiVersion"));
    m_init = reinterpret_cast<PFN_OpitLhm_Init>(GetProcAddress(mod, "OpitLhm_Init"));
    m_refresh = reinterpret_cast<PFN_OpitLhm_Refresh>(GetProcAddress(mod, "OpitLhm_Refresh"));
    m_getTempCount = reinterpret_cast<PFN_OpitLhm_GetCount>(GetProcAddress(mod, "OpitLhm_GetTemperatureCount"));
    m_getFanCount = reinterpret_cast<PFN_OpitLhm_GetCount>(GetProcAddress(mod, "OpitLhm_GetFanCount"));
    m_getTemperature = reinterpret_cast<PFN_OpitLhm_GetTemperature>(GetProcAddress(mod, "OpitLhm_GetTemperature"));
    m_getRpm = reinterpret_cast<PFN_OpitLhm_GetRpm>(GetProcAddress(mod, "OpitLhm_GetRpm"));
    m_getTempInfo = reinterpret_cast<PFN_OpitLhm_GetTemperatureInfo>(GetProcAddress(mod, "OpitLhm_GetTemperatureInfo"));
    m_getFanInfo = reinterpret_cast<PFN_OpitLhm_GetFanInfo>(GetProcAddress(mod, "OpitLhm_GetFanInfo"));
    m_setFanControl = reinterpret_cast<PFN_OpitLhm_SetFanControl>(GetProcAddress(mod, "OpitLhm_SetFanControl"));
    m_restoreFanControl = reinterpret_cast<PFN_OpitLhm_RestoreFanControl>(GetProcAddress(mod, "OpitLhm_RestoreFanControl"));
    m_shutdown = reinterpret_cast<PFN_OpitLhm_Shutdown>(GetProcAddress(mod, "OpitLhm_Shutdown"));

    if (!getAbi || !m_init || !m_refresh || !m_getTemperature || !m_getRpm || !m_shutdown ||
        getAbi() != OPIT_LHM_ABI_VERSION) {
        OPIT_LOG_ERROR("LibreHardwareMonitorFanProvider: bridge exports missing or ABI version mismatch");
        m_bridge = mod;
        UnloadBridge();
        return false;
    }

    // O LibreHardwareMonitorLib distribuido e Amd64; num processo de 32 bits a CLR recusa-o.
    // Essa recusa acontece durante a JIT das funcoes que usam os tipos da library, ou seja
    // antes de qualquer try/catch do proprio bridge existir - quem apanha o erro e o processo,
    // com fail-fast. A sonda abaixo nao referencia tipo nenhum da library, por isso o seu
    // try/catch funciona; chamar todos os outros exports so depois dela devolver 1.
    auto probe = reinterpret_cast<PFN_OpitLhm_IsLibraryLoadable>(GetProcAddress(mod, "OpitLhm_IsLibraryLoadable"));
    if (probe && !probe()) {
        OPIT_LOG_WARN("LibreHardwareMonitorFanProvider: LibreHardwareMonitorLib nao carregavel neste processo"
                      " (bitness/dotnet). Provedores ACPI, NVIDIA, AMD e Intel continuam ativos.");
        m_bridge = mod;
        UnloadBridge();
        return false;
    }

    m_bridge = mod;
    return true;
}

void LibreHardwareMonitorFanProvider::UnloadBridge() {
    // A mixed-mode (C++/CLI) DLL cannot be safely unloaded once the CLR has
    // been started in the process, so the module handle is intentionally kept
    // loaded; only the function pointers are cleared.
    m_init = nullptr;
    m_refresh = nullptr;
    m_getTempCount = nullptr;
    m_getFanCount = nullptr;
    m_getTemperature = nullptr;
    m_getRpm = nullptr;
    m_getTempInfo = nullptr;
    m_getFanInfo = nullptr;
    m_setFanControl = nullptr;
    m_restoreFanControl = nullptr;
    m_shutdown = nullptr;
}

bool LibreHardwareMonitorFanProvider::Initialize() {
    if (m_initialized) return true;
    if (!LoadBridge()) {
        OPIT_LOG_ERROR("LibreHardwareMonitorFanProvider: bridge unavailable");
        return false;
    }
    if (!m_init()) {
        OPIT_LOG_ERROR("LibreHardwareMonitorFanProvider: failed to init library");
        return false;
    }
    m_initialized = true;
    return true;
}

void LibreHardwareMonitorFanProvider::Shutdown() {
    if (!m_initialized) return;
    if (m_shutdown) m_shutdown();
    m_initialized = false;
}

std::vector<FanDeviceInfo> LibreHardwareMonitorFanProvider::EnumerateDevices() {
    std::vector<FanDeviceInfo> devices;
    if (!m_initialized) return devices;

    int32_t fanCount = m_getFanCount ? m_getFanCount() : 0;
    if (fanCount <= 0) {
        FanDeviceInfo dev;
        dev.id = kDeviceId;
        dev.name = "Sensores do Sistema (LHM)";
        dev.vendor = "LibreHardwareMonitor";
        dev.deviceType = FanDeviceType::CpuMotherboard;
        dev.capabilities.canReadTemperature = true;
        dev.capabilities.canReadRpm = true;
        dev.capabilities.canControlSpeed = (m_setFanControl != nullptr);
        dev.capabilities.canSetCurve = (m_setFanControl != nullptr);
        dev.capabilities.minSpeedPercent = 0;
        dev.capabilities.maxSpeedPercent = 100;
        dev.activeMode = FanControlMode::BiosFirmwareAuto;

        auto tel = ReadTelemetry(dev.id);
        if (tel.IsSuccess()) {
            dev.latestReading = tel.value;
        }
        devices.push_back(std::move(dev));
        return devices;
    }

    for (int32_t i = 0; i < fanCount; ++i) {
        int32_t rpm = 0;
        char nameBuf[192] = {};
        if (m_getFanInfo) {
            m_getFanInfo(i, &rpm, nameBuf, sizeof(nameBuf));
        }

        FanDeviceInfo dev;
        dev.id = "lhm_fan_" + std::to_string(i);
        std::string rawName(nameBuf);
        if (rawName.empty()) {
            dev.name = "Ventoinha #" + std::to_string(i + 1);
            dev.vendor = "Placa-Mae / Hardware";
        } else {
            size_t sep = rawName.find(" - ");
            if (sep != std::string::npos) {
                dev.vendor = rawName.substr(0, sep);
                dev.name = rawName.substr(sep + 3);
            } else {
                dev.name = rawName;
                dev.vendor = "Hardware Monitor";
            }
        }

        std::string lowerName = StringUtils::ToLower(dev.name);
        std::string lowerVendor = StringUtils::ToLower(dev.vendor);
        if (lowerName.find("nvidia") != std::string::npos || lowerVendor.find("nvidia") != std::string::npos) {
            dev.deviceType = FanDeviceType::GpuNvidia;
        } else if (lowerName.find("amd") != std::string::npos || lowerVendor.find("amd") != std::string::npos || lowerVendor.find("radeon") != std::string::npos) {
            dev.deviceType = FanDeviceType::GpuAmd;
        } else if (lowerName.find("intel") != std::string::npos && (lowerName.find("gpu") != std::string::npos || lowerName.find("arc") != std::string::npos)) {
            dev.deviceType = FanDeviceType::GpuIntel;
        } else {
            dev.deviceType = FanDeviceType::CpuMotherboard;
        }

        dev.capabilities.canReadTemperature = true;
        dev.capabilities.canReadRpm = true;
        dev.capabilities.canControlSpeed = (m_setFanControl != nullptr);
        dev.capabilities.canSetCurve = (m_setFanControl != nullptr);
        dev.capabilities.minSpeedPercent = 0;
        dev.capabilities.maxSpeedPercent = 100;
        dev.activeMode = FanControlMode::BiosFirmwareAuto;

        auto tel = ReadTelemetry(dev.id);
        if (tel.IsSuccess()) {
            dev.latestReading = tel.value;
        }

        devices.push_back(std::move(dev));
    }

    return devices;
}

FanResult<FanReading> LibreHardwareMonitorFanProvider::ReadTelemetry(const std::string &deviceId) {
    FanResult<FanReading> result;
    if (!m_initialized) {
        result.code = FanStatusCode::Unavailable;
        result.message = "Provider not initialized";
        return result;
    }

    int32_t targetFanIndex = -1;
    if (deviceId.rfind("lhm_fan_", 0) == 0) {
        try {
            targetFanIndex = std::stoi(deviceId.substr(8));
        } catch (...) {
            targetFanIndex = 0;
        }
    } else if (deviceId != kDeviceId) {
        result.code = FanStatusCode::Unsupported;
        result.message = "Unknown device ID";
        return result;
    }

    FanReading reading;
    reading.timestampMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    // Update sensor values before reading.
    m_refresh();

    // 1. Ler RPM da ventoinha específica
    char fanNameBuf[192] = {};
    if (targetFanIndex >= 0 && m_getFanInfo) {
        int32_t rpm = 0;
        if (m_getFanInfo(targetFanIndex, &rpm, fanNameBuf, sizeof(fanNameBuf))) {
            reading.rpm = (rpm >= 0) ? static_cast<uint32_t>(rpm) : 0;
            reading.hasRpm = true;
        }
    } else if (m_getFanInfo) {
        int32_t fCount = m_getFanCount ? m_getFanCount() : 1;
        for (int32_t i = 0; i < fCount; ++i) {
            int32_t rpm = 0;
            char nb[192] = {};
            if (m_getFanInfo(i, &rpm, nb, sizeof(nb)) && rpm > 0) {
                reading.rpm = static_cast<uint32_t>(rpm);
                reading.hasRpm = true;
                strncpy_s(fanNameBuf, nb, sizeof(fanNameBuf));
                break;
            }
        }
    }

    // 2. Determinar temperatura correspondente com base no tipo de ventoinha
    std::string sFanName(fanNameBuf);
    std::string lowerFan = StringUtils::ToLower(sFanName);
    bool isGpuFan = (lowerFan.find("gpu") != std::string::npos || lowerFan.find("nvidia") != std::string::npos || lowerFan.find("radeon") != std::string::npos);

    int32_t tCount = m_getTempCount ? m_getTempCount() : 1;
    std::string chosenTempName;
    double bestTemp = 0.0;

    for (int32_t i = 0; i < tCount; ++i) {
        double temp = 0.0;
        char nameBuf[192] = {};
        bool ok = m_getTempInfo ? (m_getTempInfo(i, &temp, nameBuf, sizeof(nameBuf)) != 0)
                                : (m_getTemperature(i, &temp) != 0);
        if (ok && temp > 0.5 && temp < 130.0) {
            std::string sName(nameBuf);
            std::string lowerTName = StringUtils::ToLower(sName);

            if (isGpuFan) {
                if (lowerTName.find("gpu") != std::string::npos && lowerTName.find("hot spot") == std::string::npos) {
                    bestTemp = temp;
                    chosenTempName = sName;
                    break;
                }
            } else {
                if (lowerTName.find("package") != std::string::npos || lowerTName.find("core (tctl") != std::string::npos || lowerTName.find("cpu core") != std::string::npos) {
                    bestTemp = temp;
                    chosenTempName = sName;
                    break;
                }
            }
            if (bestTemp <= 0.5) {
                bestTemp = temp;
                chosenTempName = sName;
            }
        }
    }

    if (bestTemp > 0.5) {
        reading.temperatureC = bestTemp;
        reading.hasTemperature = true;
    }

    reading.sensorSource = "LibreHardwareMonitor (" + 
                           (!chosenTempName.empty() ? chosenTempName : "Temp") + " | " +
                           (!sFanName.empty() ? sFanName : "RPM") + ")";

    result.value = std::move(reading);
    result.code = FanStatusCode::Ok;
    return result;
}

FanResult<bool> LibreHardwareMonitorFanProvider::SetSpeedPercent(const std::string &deviceId, uint32_t speedPercent) {
    FanResult<bool> res;
    if (!m_initialized || !m_setFanControl) {
        res.code = FanStatusCode::Unsupported;
        res.message = "LibreHardwareMonitor fan control not available";
        res.value = false;
        return res;
    }

    // FAIL-SAFE DE SEGURANÇA TÉRMICA DE HARDWARE (CIRCUIT BREAKER)
    // Se a temperatura do componente estiver acima de 65°C, rejeita rotações perigosas (< 30%).
    // Se estiver acima de 85°C, força restauração de controle nativo da BIOS/VBIOS para proteger o silício!
    double currentMaxTemp = 0.0;
    if (m_refresh && m_getTempCount && m_getTemperature) {
        m_refresh();
        int32_t tCount = m_getTempCount();
        for (int32_t i = 0; i < tCount; ++i) {
            double tVal = 0.0;
            if (m_getTemperature(i, &tVal) && tVal > currentMaxTemp) {
                currentMaxTemp = tVal;
            }
        }
    }

    if (currentMaxTemp >= 85.0) {
        // Alerta Crítico Térmico: Hardware em temperatura de risco extremo -> Devolve controle para a BIOS imediatamente!
        OPIT_LOG_ERROR("Fail-Safe Termico Ativo: Temperatura atingiu " + std::to_string(currentMaxTemp) + 
                       " C. Comando manual cancelado e controle restaurado para firmware nativo.");
        RestoreDefaultControl(deviceId);
        res.code = FanStatusCode::CommandRejected;
        res.message = "Hardware safety guardrail: thermal emergency, fan restored to BIOS/VBIOS control";
        res.value = false;
        return res;
    }

    if (currentMaxTemp >= 65.0 && speedPercent < 30) {
        // Bloqueia comandos de parada ou rotação fraca quando o hardware está aquecido
        speedPercent = 50; // eleva automaticamente para patamar seguro
        OPIT_LOG_CAT(opitcore::LogLevel::Warning, "fans", "Fail-Safe Termico Ativo: Rotacao solicitada muito baixa sob carga (" + 
                     std::to_string(currentMaxTemp) + " C). Forcando velocidade minima segura de 50%.");
    }

    int32_t targetFanIndex = 0;
    if (deviceId.rfind("lhm_fan_", 0) == 0) {
        try {
            targetFanIndex = std::stoi(deviceId.substr(8));
        } catch (...) {
            targetFanIndex = 0;
        }
    }

    int32_t ok = m_setFanControl(targetFanIndex, static_cast<double>(speedPercent));
    if (ok) {
        res.code = FanStatusCode::Ok;
        res.value = true;
    } else {
        res.code = FanStatusCode::Unsupported;
        res.message = "Super I/O or GPU controller did not accept software speed control on this header";
        res.value = false;
    }
    return res;
}

FanResult<bool> LibreHardwareMonitorFanProvider::RestoreDefaultControl(const std::string &deviceId) {
    FanResult<bool> res;
    if (!m_initialized || !m_restoreFanControl) {
        res.code = FanStatusCode::Ok;
        res.value = true;
        return res;
    }

    int32_t targetFanIndex = 0;
    if (deviceId.rfind("lhm_fan_", 0) == 0) {
        try {
            targetFanIndex = std::stoi(deviceId.substr(8));
        } catch (...) {
            targetFanIndex = 0;
        }
    }

    m_restoreFanControl(targetFanIndex);
    res.code = FanStatusCode::Ok;
    res.value = true;
    return res;
}

} // namespace opitcore
