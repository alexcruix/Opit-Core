// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "AmdAdlFanProvider.h"
#include <chrono>
#include <cstdlib>

namespace opitcore {

struct AdapterInfo {
    int iSize;
    int iAdapterIndex;
    char strUDID[256];
    int iBusNumber;
    int iDeviceNumber;
    int iFunctionNumber;
    int iVendorID;
    char strAdapterName[256];
    char strDisplayName[256];
    int iPresent;
    int iExist;
    char strDriverPath[256];
    char strDriverPathExt[256];
    char strPNPString[256];
    int iOSDisplayIndex;
};

struct ADLTemperature {
    int iSize;
    int iTemperature; // Temperatura em milésimos de Celsius (50000 = 50°C)
};

struct ADLFanSpeedValue {
    int iSize;
    int iSpeedType; // ADL_DL_FANCTRL_SPEED_TYPE_PERCENT (1) ou RPM (2)
    int iFanSpeed;
    int iFlags;
};

// ADL e __stdcall no Win32: sem a convencao no typedef o x86 recusa o endereco da callback (C2664).
typedef void* (__stdcall *ADL_MAIN_MALLOC_CALLBACK)(int);
typedef int (*ADL_Main_Control_Create_t)(ADL_MAIN_MALLOC_CALLBACK, int);
typedef int (*ADL_Main_Control_Destroy_t)();
typedef int (*ADL_Adapter_NumberOfAdapters_Get_t)(int*);
typedef int (*ADL_Adapter_AdapterInfo_Get_t)(AdapterInfo*, int);
typedef int (*ADL_Overdrive5_Temperature_Get_t)(int, int, ADLTemperature*);
typedef int (*ADL_Overdrive5_FanSpeed_Get_t)(int, int, ADLFanSpeedValue*);
typedef int (*ADL_Overdrive5_FanSpeed_Set_t)(int, int, ADLFanSpeedValue*);
typedef int (*ADL_Overdrive5_FanSpeedToDefault_Set_t)(int, int);

static void* __stdcall ADL_Main_Memory_Alloc(int iSize) {
    return malloc(iSize);
}

AmdAdlFanProvider::AmdAdlFanProvider() = default;

AmdAdlFanProvider::~AmdAdlFanProvider() {
    Shutdown();
}

bool AmdAdlFanProvider::Initialize() {
    if (m_initialized) return true;

    m_hAdlDll = LoadLibraryW(L"atiadlxx.dll");
    if (!m_hAdlDll) {
        m_hAdlDll = LoadLibraryW(L"atiadlxy.dll");
    }

    if (!m_hAdlDll) {
        return false;
    }

    auto adlCreate = (ADL_Main_Control_Create_t)GetProcAddress(m_hAdlDll, "ADL_Main_Control_Create");
    if (!adlCreate || adlCreate(ADL_Main_Memory_Alloc, 1) != 0) {
        FreeLibrary(m_hAdlDll);
        m_hAdlDll = nullptr;
        return false;
    }

    auto getNumAdapters = (ADL_Adapter_NumberOfAdapters_Get_t)GetProcAddress(m_hAdlDll, "ADL_Adapter_NumberOfAdapters_Get");
    auto getAdapterInfo = (ADL_Adapter_AdapterInfo_Get_t)GetProcAddress(m_hAdlDll, "ADL_Adapter_AdapterInfo_Get");

    if (getNumAdapters && getAdapterInfo) {
        int numAdapters = 0;
        if (getNumAdapters(&numAdapters) == 0 && numAdapters > 0) {
            std::vector<AdapterInfo> infos(numAdapters);
            memset(infos.data(), 0, sizeof(AdapterInfo) * numAdapters);
            infos[0].iSize = sizeof(AdapterInfo);
            if (getAdapterInfo(infos.data(), sizeof(AdapterInfo) * numAdapters) == 0) {
                for (int i = 0; i < numAdapters; ++i) {
                    if (infos[i].iPresent && infos[i].iVendorID == 0x1002) {
                        m_adapterIndices.push_back(infos[i].iAdapterIndex);
                        m_adapterNames.push_back(std::string(infos[i].strAdapterName));
                    }
                }
            }
        }
    }

    m_initialized = true;
    return true;
}

void AmdAdlFanProvider::Shutdown() {
    if (m_initialized && m_hAdlDll) {
        auto adlDestroy = (ADL_Main_Control_Destroy_t)GetProcAddress(m_hAdlDll, "ADL_Main_Control_Destroy");
        if (adlDestroy) {
            adlDestroy();
        }
        FreeLibrary(m_hAdlDll);
        m_hAdlDll = nullptr;
    }
    m_adapterIndices.clear();
    m_adapterNames.clear();
    m_initialized = false;
}

std::vector<FanDeviceInfo> AmdAdlFanProvider::EnumerateDevices() {
    std::vector<FanDeviceInfo> devices;
    if (!m_initialized || m_adapterIndices.empty()) {
        return devices;
    }

    for (size_t i = 0; i < m_adapterIndices.size(); ++i) {
        FanDeviceInfo dev;
        dev.id = "gpu:amd:" + std::to_string(i);
        dev.name = m_adapterNames[i].empty() ? "AMD Radeon GPU #" + std::to_string(i) : m_adapterNames[i];
        dev.vendor = "AMD";
        dev.deviceType = FanDeviceType::GpuAmd;

        dev.capabilities.canReadTemperature = true;
        dev.capabilities.canReadRpm = true;
        dev.capabilities.canControlSpeed = true;
        dev.capabilities.canSetCurve = true;
        dev.capabilities.minSpeedPercent = 20;
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

FanResult<FanReading> AmdAdlFanProvider::ReadTelemetry(const std::string& deviceId) {
    FanResult<FanReading> res;
    if (!m_initialized) {
        res.code = FanStatusCode::Unavailable;
        res.message = "Driver AMD ADL nao carregado.";
        return res;
    }

    size_t idx = 0;
    try {
        if (deviceId.rfind("gpu:amd:", 0) == 0) {
            idx = std::stoul(deviceId.substr(8));
        } else {
            res.code = FanStatusCode::Unavailable;
            return res;
        }
    } catch (...) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    if (idx >= m_adapterIndices.size()) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    int adapterIndex = m_adapterIndices[idx];
    FanReading r;
    r.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    r.sensorSource = "AMD ADL Overdrive";

    // Temperatura
    auto getTemp = (ADL_Overdrive5_Temperature_Get_t)GetProcAddress(m_hAdlDll, "ADL_Overdrive5_Temperature_Get");
    if (getTemp) {
        ADLTemperature t{};
        t.iSize = sizeof(ADLTemperature);
        if (getTemp(adapterIndex, 0, &t) == 0) {
            r.hasTemperature = true;
            r.temperatureC = t.iTemperature / 1000.0;
        }
    }

    // RPM
    auto getFanSpeed = (ADL_Overdrive5_FanSpeed_Get_t)GetProcAddress(m_hAdlDll, "ADL_Overdrive5_FanSpeed_Get");
    if (getFanSpeed) {
        ADLFanSpeedValue fsRpm{};
        fsRpm.iSize = sizeof(ADLFanSpeedValue);
        fsRpm.iSpeedType = 2; // RPM
        if (getFanSpeed(adapterIndex, 0, &fsRpm) == 0 && fsRpm.iFanSpeed >= 0) {
            r.hasRpm = true;
            r.rpm = (uint32_t)fsRpm.iFanSpeed;
        }

        ADLFanSpeedValue fsPct{};
        fsPct.iSize = sizeof(ADLFanSpeedValue);
        fsPct.iSpeedType = 1; // Percentual
        if (getFanSpeed(adapterIndex, 0, &fsPct) == 0 && fsPct.iFanSpeed >= 0) {
            r.hasReportedSpeedPercent = true;
            r.reportedSpeedPercent = (uint32_t)fsPct.iFanSpeed;
        }
    }

    res.code = (r.hasTemperature || r.hasRpm) ? FanStatusCode::Ok : FanStatusCode::ReadError;
    res.value = r;
    return res;
}

FanResult<bool> AmdAdlFanProvider::SetSpeedPercent(const std::string& deviceId, uint32_t speedPercent) {
    FanResult<bool> res;
    if (!m_initialized) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    size_t idx = 0;
    try {
        idx = std::stoul(deviceId.substr(8));
    } catch (...) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    if (idx >= m_adapterIndices.size()) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    if (speedPercent < 20) speedPercent = 20;
    if (speedPercent > 100) speedPercent = 100;

    auto setFanSpeed = (ADL_Overdrive5_FanSpeed_Set_t)GetProcAddress(m_hAdlDll, "ADL_Overdrive5_FanSpeed_Set");
    if (!setFanSpeed) {
        res.code = FanStatusCode::Unsupported;
        res.message = "Funcao ADL_Overdrive5_FanSpeed_Set indisponivel no driver AMD instalado.";
        return res;
    }

    ADLFanSpeedValue val{};
    val.iSize = sizeof(ADLFanSpeedValue);
    val.iSpeedType = 1; // Percent
    val.iFanSpeed = (int)speedPercent;
    val.iFlags = 1;

    int status = setFanSpeed(m_adapterIndices[idx], 0, &val);
    if (status == 0) {
        res.code = FanStatusCode::Ok;
        res.value = true;
        res.message = "Velocidade de ventoinha aplicada com sucesso via AMD ADL.";
    } else {
        res.code = FanStatusCode::CommandRejected;
        res.value = false;
        res.message = "Driver AMD rejeitou a velocidade solicitada.";
    }
    return res;
}

FanResult<bool> AmdAdlFanProvider::RestoreDefaultControl(const std::string& deviceId) {
    FanResult<bool> res;
    if (!m_initialized) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    size_t idx = 0;
    try {
        idx = std::stoul(deviceId.substr(8));
    } catch (...) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    if (idx >= m_adapterIndices.size()) {
        res.code = FanStatusCode::Unavailable;
        return res;
    }

    auto restoreDefault = (ADL_Overdrive5_FanSpeedToDefault_Set_t)GetProcAddress(m_hAdlDll, "ADL_Overdrive5_FanSpeedToDefault_Set");
    if (restoreDefault) {
        restoreDefault(m_adapterIndices[idx], 0);
    }

    res.code = FanStatusCode::Ok;
    res.value = true;
    res.message = "Controle automatico devolvido ao firmware da GPU AMD.";
    return res;
}

} // namespace opitcore
