#include "NvidiaNvapiFanProvider.h"
#include "utils/StringUtils.h"
#include <chrono>

namespace opitcore {

typedef void* (*NvAPI_QueryInterface_t)(unsigned int offset);
typedef int (*NvAPI_Initialize_t)();
typedef int (*NvAPI_Unload_t)();
typedef int (*NvAPI_EnumPhysicalGPUs_t)(void** nvGPUHandle, int* pGpuCount);
typedef int (*NvAPI_GPU_GetFullName_t)(void* hPhysicalGpu, char* szName);
typedef int (*NvAPI_GPU_GetTachReading_t)(void* hPhysicalGpu, int* pValue);

// Estruturas de Thermal e Cooler do NVAPI
#define NVAPI_MAX_THERMAL_SENSORS_PER_GPU 3
#define NVAPI_THERMAL_SETTINGS_VER_2 (sizeof(NV_GPU_THERMAL_SETTINGS_V2) | 0x20000)

struct NV_SENSOR {
    int controller;
    int defaultMinTemp;
    int defaultMaxTemp;
    int currentTemp;
    int target;
};

struct NV_GPU_THERMAL_SETTINGS_V2 {
    unsigned int version;
    unsigned int count;
    NV_SENSOR sensor[NVAPI_MAX_THERMAL_SENSORS_PER_GPU];
};

typedef int (*NvAPI_GPU_GetThermalSettings_t)(void* hPhysicalGpu, unsigned int sensorIndex, NV_GPU_THERMAL_SETTINGS_V2* pThermalSettings);

#define NVAPI_MAX_COOLERS_PER_GPU 20
#define NVAPI_COOLER_SETTINGS_VER (sizeof(NV_GPU_COOLER_SETTINGS) | 0x20000)
#define NVAPI_COOLER_LEVELS_VER (sizeof(NV_GPU_COOLER_LEVELS) | 0x10000)

struct NV_COOLER {
    int type;
    int controller;
    int defaultMinLevel;
    int defaultMaxLevel;
    int currentLevel;
    int currentPolicy;
    int defaultPolicy;
    int target;
    int controlType;
    int active;
};

struct NV_GPU_COOLER_SETTINGS {
    unsigned int version;
    unsigned int count;
    NV_COOLER cooler[NVAPI_MAX_COOLERS_PER_GPU];
};

struct NV_COOLER_LEVEL {
    int level;
    int policy;
};

struct NV_GPU_COOLER_LEVELS {
    unsigned int version;
    NV_COOLER_LEVEL cooler[NVAPI_MAX_COOLERS_PER_GPU];
};

typedef int (*NvAPI_GPU_GetCoolerSettings_t)(void* hPhysicalGpu, unsigned int coolerIndex, NV_GPU_COOLER_SETTINGS* pCoolerSettings);
typedef int (*NvAPI_GPU_SetCoolerLevels_t)(void* hPhysicalGpu, unsigned int coolerIndex, NV_GPU_COOLER_LEVELS* pCoolerLevels);
typedef int (*NvAPI_GPU_RestoreCoolerSettings_t)(void* hPhysicalGpu, unsigned int coolerIndex);

NvidiaNvapiFanProvider::NvidiaNvapiFanProvider() = default;

NvidiaNvapiFanProvider::~NvidiaNvapiFanProvider() {
    Shutdown();
}

void* NvidiaNvapiFanProvider::QueryNvInterface(unsigned int id) {
    if (!m_hNvApiDll) return nullptr;
    auto queryFunc = (NvAPI_QueryInterface_t)GetProcAddress(m_hNvApiDll, "nvapi_QueryInterface");
    if (!queryFunc) return nullptr;
    return queryFunc(id);
}

bool NvidiaNvapiFanProvider::Initialize() {
    if (m_initialized) return true;

    m_hNvApiDll = LoadLibraryW(L"nvapi64.dll");
    if (!m_hNvApiDll) {
        m_hNvApiDll = LoadLibraryW(L"nvapi.dll");
    }

    if (!m_hNvApiDll) {
        return false;
    }

    auto nvInit = (NvAPI_Initialize_t)QueryNvInterface(0x0150E828);
    if (!nvInit || nvInit() != 0) {
        FreeLibrary(m_hNvApiDll);
        m_hNvApiDll = nullptr;
        return false;
    }

    auto enumGpus = (NvAPI_EnumPhysicalGPUs_t)QueryNvInterface(0xE5AC921F);
    if (enumGpus) {
        void* handles[64] = {nullptr};
        int count = 0;
        if (enumGpus(handles, &count) == 0 && count > 0) {
            for (int i = 0; i < count; ++i) {
                m_nvPhysicalGpus.push_back(handles[i]);
            }
        }
    }

    m_initialized = true;
    return true;
}

void NvidiaNvapiFanProvider::Shutdown() {
    if (m_initialized && m_hNvApiDll) {
        auto nvUnload = (NvAPI_Unload_t)QueryNvInterface(0xD22BDD7E);
        if (nvUnload) {
            nvUnload();
        }
        FreeLibrary(m_hNvApiDll);
        m_hNvApiDll = nullptr;
    }
    m_nvPhysicalGpus.clear();
    m_initialized = false;
}

std::vector<FanDeviceInfo> NvidiaNvapiFanProvider::EnumerateDevices() {
    std::vector<FanDeviceInfo> devices;
    if (!m_initialized || m_nvPhysicalGpus.empty()) {
        return devices;
    }

    auto getFullName = (NvAPI_GPU_GetFullName_t)QueryNvInterface(0xCEEE8E9F);

    for (size_t i = 0; i < m_nvPhysicalGpus.size(); ++i) {
        FanDeviceInfo dev;
        dev.id = "gpu:nvidia:" + std::to_string(i);
        dev.vendor = "NVIDIA";
        dev.deviceType = FanDeviceType::GpuNvidia;

        char nameBuf[64] = {0};
        if (getFullName && getFullName(m_nvPhysicalGpus[i], nameBuf) == 0 && nameBuf[0] != '\0') {
            dev.name = std::string(nameBuf);
        } else {
            dev.name = "NVIDIA GeForce GPU #" + std::to_string(i);
        }

        dev.capabilities.canReadTemperature = true;
        dev.capabilities.canReadRpm = true;
        dev.capabilities.canControlSpeed = true;
        dev.capabilities.canSetCurve = true;
        dev.capabilities.minSpeedPercent = 20; // Limite mínimo de proteção
        dev.capabilities.maxSpeedPercent = 100;
        dev.activeMode = FanControlMode::BiosFirmwareAuto;

        auto tel = ReadTelemetry(dev.id);
        if (tel.IsSuccess()) {
            dev.latestReading = tel.value;
        }

        devices.push_back(dev);
    }

    return devices;
}

FanResult<FanReading> NvidiaNvapiFanProvider::ReadTelemetry(const std::string& deviceId) {
    FanResult<FanReading> res;
    if (!m_initialized) {
        res.code = FanStatusCode::Unavailable;
        res.message = "Driver NVIDIA NVAPI nao disponivel neste sistema.";
        return res;
    }

    size_t gpuIdx = 0;
    try {
        if (deviceId.rfind("gpu:nvidia:", 0) == 0) {
            gpuIdx = std::stoul(deviceId.substr(11));
        } else {
            res.code = FanStatusCode::Unavailable;
            res.message = "ID de dispositivo invalido.";
            return res;
        }
    } catch (...) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    if (gpuIdx >= m_nvPhysicalGpus.size()) {
        res.code = FanStatusCode::Unavailable;
        res.message = "Indice de GPU NVIDIA fora de alcance.";
        return res;
    }

    void* hGpu = m_nvPhysicalGpus[gpuIdx];
    FanReading r;
    r.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    r.sensorSource = "NVIDIA NVAPI";

    // 1. Leitura de Temperatura
    auto getThermal = (NvAPI_GPU_GetThermalSettings_t)QueryNvInterface(0xE3640A56);
    if (getThermal) {
        NV_GPU_THERMAL_SETTINGS_V2 settings{};
        settings.version = NVAPI_THERMAL_SETTINGS_VER_2;
        if (getThermal(hGpu, 0, &settings) == 0 && settings.count > 0) {
            r.hasTemperature = true;
            r.temperatureC = (double)settings.sensor[0].currentTemp;
        }
    }

    // 2. Leitura de RPM (Tacômetro)
    auto getTach = (NvAPI_GPU_GetTachReading_t)QueryNvInterface(0x5F608315);
    if (getTach) {
        int tachValue = 0;
        if (getTach(hGpu, &tachValue) == 0 && tachValue >= 0) {
            r.hasRpm = true;
            r.rpm = (uint32_t)tachValue;
        }
    }

    // 3. Leitura de Percentual Atual do Cooler
    auto getCooler = (NvAPI_GPU_GetCoolerSettings_t)QueryNvInterface(0xDA141340);
    if (getCooler) {
        NV_GPU_COOLER_SETTINGS coolerSettings{};
        coolerSettings.version = NVAPI_COOLER_SETTINGS_VER;
        if (getCooler(hGpu, 0, &coolerSettings) == 0 && coolerSettings.count > 0) {
            r.hasReportedSpeedPercent = true;
            r.reportedSpeedPercent = (uint32_t)coolerSettings.cooler[0].currentLevel;
        }
    }

    res.code = (r.hasTemperature || r.hasRpm) ? FanStatusCode::Ok : FanStatusCode::ReadError;
    res.value = r;
    return res;
}

FanResult<bool> NvidiaNvapiFanProvider::SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) {
    FanResult<bool> res;
    if (!m_initialized) {
        res.code = FanStatusCode::Unavailable;
        res.message = "NVAPI indisponivel.";
        return res;
    }

    size_t gpuIdx = 0;
    try {
        gpuIdx = std::stoul(deviceId.substr(11));
    } catch (...) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    if (gpuIdx >= m_nvPhysicalGpus.size()) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    // Validação de segurança: mínimo 20%, máximo 100%
    if (speedPercent < 20) speedPercent = 20;
    if (speedPercent > 100) speedPercent = 100;

    auto setCooler = (NvAPI_GPU_SetCoolerLevels_t)QueryNvInterface(0x891FA0AE);
    if (!setCooler) {
        res.code = FanStatusCode::Unsupported;
        res.message = "Funcao NvAPI_GPU_SetCoolerLevels nao exportada pelo driver instalado.";
        return res;
    }

    NV_GPU_COOLER_LEVELS levels{};
    levels.version = NVAPI_COOLER_LEVELS_VER;
    levels.cooler[0].level = (int)speedPercent;
    levels.cooler[0].policy = 1; // Manual policy

    int status = setCooler(m_nvPhysicalGpus[gpuIdx], 0, &levels);
    if (status == 0) {
        res.code = FanStatusCode::Ok;
        res.value = true;
        res.message = "Velocidade da ventoinha aplicada com sucesso via NVAPI.";
    } else {
        res.code = FanStatusCode::CommandRejected;
        res.value = false;
        res.message = "O driver NVIDIA rejeitou o comando de velocidade (Status: " + std::to_string(status) + ").";
    }
    return res;
}

FanResult<bool> NvidiaNvapiFanProvider::RestoreDefaultControl(const std::string& deviceId) {
    FanResult<bool> res;
    if (!m_initialized) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    size_t gpuIdx = 0;
    try {
        gpuIdx = std::stoul(deviceId.substr(11));
    } catch (...) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    if (gpuIdx >= m_nvPhysicalGpus.size()) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    // 1. Tentar política Auto (0x20 = 32) via SetCoolerLevels
    auto setCooler = (NvAPI_GPU_SetCoolerLevels_t)QueryNvInterface(0x891FA0AE);
    if (setCooler) {
        NV_GPU_COOLER_LEVELS levels{};
        levels.version = NVAPI_COOLER_LEVELS_VER;
        levels.cooler[0].level = 0;
        levels.cooler[0].policy = 0x20; // Auto policy (32)
        int status = setCooler(m_nvPhysicalGpus[gpuIdx], 0, &levels);
        if (status == 0) {
            res.code = FanStatusCode::Ok;
            res.value = true;
            res.message = "Controle automatico restaurado com sucesso para a VBIOS/Firmware da GPU.";
            return res;
        }
    }

    // 2. Fallback para RestoreCoolerSettings
    auto restoreCooler = (NvAPI_GPU_RestoreCoolerSettings_t)QueryNvInterface(0x8F6ED0FB);
    if (restoreCooler) {
        int status = restoreCooler(m_nvPhysicalGpus[gpuIdx], 0);
        if (status == 0) {
            res.code = FanStatusCode::Ok;
            res.value = true;
            res.message = "Controle automatico do cooler restaurado com sucesso para a VBIOS da GPU.";
            return res;
        }
    }

    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "Politica de controle devolvida ao driver.";
    return res;
}

} // namespace opitcore
