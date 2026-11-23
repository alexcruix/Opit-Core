// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

// Todo este ficheiro e o caminho Windows: NVAPI, ADL/ADLX e IGCL so existem como
// bibliotecas do Windows, carregadas por LoadLibraryW. O gemelo do Linux, com NVML e
// sysfs, esta em GpuControlManagerLinux.cpp e preenche a mesma classe.
#ifdef _WIN32
#include "GpuControlManager.h"
#include "platform/windows/GpuDetector.h"
#include "core/Logger.h"
#include "utils/StringUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace opitcore {

// ============================================================================
// 1. Definições e Assinaturas NVAPI (NVIDIA)
// ============================================================================
typedef void* (*NvAPI_QueryInterface_t)(unsigned int offset);
typedef int (*NvAPI_Initialize_t)();
typedef int (*NvAPI_Unload_t)();
typedef int (*NvAPI_EnumPhysicalGPUs_t)(void** nvGPUHandle, int* pGpuCount);
typedef int (*NvAPI_GPU_GetFullName_t)(void* hPhysicalGpu, char* szName);
typedef int (*NvAPI_GPU_GetThermalSettings_t)(void* hPhysicalGpu, unsigned int sensorIndex, void* pThermalSettings);
typedef int (*NvAPI_GPU_GetAllClockFrequencies_t)(void* hPhysicalGpu, void* pClkFreqs);
typedef int (*NvAPI_GPU_GetDynamicPstatesInfoEx_t)(void* hPhysicalGpu, void* pDynamicPstatesInfoEx);
typedef int (*NvAPI_GPU_GetPstates20_t)(void* hPhysicalGpu, void* pPstatesInfo);
typedef int (*NvAPI_GPU_SetPstates20_t)(void* hPhysicalGpu, void* pPstatesInfo);
typedef int (*NvAPI_GPU_ClientPowerTopologyGetStatus_t)(void* hPhysicalGpu, void* pPowerTopo);
typedef int (*NvAPI_GPU_ClientPowerTopologySetStatus_t)(void* hPhysicalGpu, void* pPowerTopo);
typedef int (*NvAPI_GPU_ClientPowerPoliciesGetInfo_t)(void* hPhysicalGpu, void* pPowerPoliciesInfo);
typedef int (*NvAPI_GPU_ClientPowerPoliciesSetStatus_t)(void* hPhysicalGpu, void* pPowerPoliciesStatus);

#define NVAPI_MAX_THERMAL_SENSORS_PER_GPU 3
#define NVAPI_MAX_GPU_PUBLIC_CLOCKS 32
#define NVAPI_MAX_GPU_UTILIZATIONS 8
#define NVAPI_MAX_GPU_PSTATE20_PSTATES 16
#define NVAPI_MAX_GPU_PSTATE20_CLOCKS 8
#define NVAPI_MAX_GPU_PSTATE20_BASE_VOLTAGES 4

struct NV_SENSOR_INTERNAL {
    int controller;
    int defaultMinTemp;
    int defaultMaxTemp;
    int currentTemp;
    int target;
};

struct NV_GPU_THERMAL_SETTINGS_INTERNAL {
    unsigned int version;
    unsigned int count;
    NV_SENSOR_INTERNAL sensor[NVAPI_MAX_THERMAL_SENSORS_PER_GPU];
};

struct NV_GPU_CLOCK_FREQUENCIES_INTERNAL {
    unsigned int version;
    unsigned int ClockType:4;
    unsigned int reserved:20;
    unsigned int reserved1:8;
    struct {
        unsigned int bIsPresent:1;
        unsigned int reserved:31;
        unsigned int frequency; // kHz
    } domain[NVAPI_MAX_GPU_PUBLIC_CLOCKS];
};

struct NV_GPU_DYNAMIC_PSTATES_INFO_EX_INTERNAL {
    unsigned int version;
    unsigned int flags;
    struct {
        unsigned int bIsPresent:1;
        unsigned int percentage;
    } utilization[NVAPI_MAX_GPU_UTILIZATIONS];
};

struct NV_GPU_PERF_PSTATES20_PARAM_DELTA_INT {
    int value;
    struct {
        int min;
        int max;
    } valueRange;
};

struct NV_GPU_PSTATE20_CLOCK_ENTRY_INT {
    unsigned int domainId; // 0 = Graphics (Core), 4 = Memory
    unsigned int typeId;
    unsigned int bIsEditable:1;
    unsigned int reserved:31;
    NV_GPU_PERF_PSTATES20_PARAM_DELTA_INT freqDelta_kHz;
    union {
        struct { unsigned int freq_kHz; } single;
        struct {
            unsigned int minFreq_kHz;
            unsigned int maxFreq_kHz;
            unsigned int domainId;
            unsigned int minVoltage_uV;
            unsigned int maxVoltage_uV;
        } range;
    } data;
};

struct NV_GPU_PSTATE20_BASE_VOLTAGE_ENTRY_INT {
    unsigned int domainId;
    unsigned int bIsEditable:1;
    unsigned int reserved:31;
    unsigned int volt_uV;
    NV_GPU_PERF_PSTATES20_PARAM_DELTA_INT voltDelta_uV;
};

struct NV_GPU_PERF_PSTATES20_INFO_INT {
    unsigned int version;
    unsigned int bIsEditable:1;
    unsigned int reserved:31;
    unsigned int numPstates;
    unsigned int numClocks;
    unsigned int numBaseVoltages;
    struct {
        unsigned int pstateId; // 0 = P0 (Max 3D Performance)
        unsigned int bIsEditable:1;
        unsigned int reserved:31;
        NV_GPU_PSTATE20_CLOCK_ENTRY_INT clocks[NVAPI_MAX_GPU_PSTATE20_CLOCKS];
        NV_GPU_PSTATE20_BASE_VOLTAGE_ENTRY_INT baseVoltages[NVAPI_MAX_GPU_PSTATE20_BASE_VOLTAGES];
    } pstates[NVAPI_MAX_GPU_PSTATE20_PSTATES];
};

struct NV_GPU_POWER_POLICIES_STATUS_ENTRY_INT {
    unsigned int performanceStateId;
    unsigned int powerPolicy; // percent * 1000 (100% = 100000)
};

struct NV_GPU_POWER_POLICIES_STATUS_INT {
    unsigned int version;
    unsigned int count;
    NV_GPU_POWER_POLICIES_STATUS_ENTRY_INT entries[4];
};

struct NV_GPU_POWER_TOPOLOGY_ENTRY_INT {
    unsigned int domain;
    unsigned int power;
};

struct NV_GPU_POWER_TOPOLOGY_STATUS_INT {
    unsigned int version;
    unsigned int count;
    NV_GPU_POWER_TOPOLOGY_ENTRY_INT entries[4];
};

// ============================================================================
// 2. Definições e Assinaturas AMD ADL (AMD)
// ============================================================================
struct ADLAdapterInfoInternal {
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

struct ADLTemperatureInternal {
    int iSize;
    int iTemperature; // Em milésimos de Celsius (ex: 50000 = 50°C)
};

struct ADLPMActivityInternal {
    int iSize;
    int iEngineClock;       // Core Clock em 10 KHz (ex: 180000 = 1800 MHz)
    int iMemoryClock;       // Memory Clock em 10 KHz
    int iVddc;              // Tensão em mV
    int iActivityPercent;   // Utilização em % (0 a 100)
    int iCurrentPerformanceLevel;
    int iCurrentBusSpeed;
    int iCurrentBusLanes;
    int iMaximumBusLanes;
    int iReserved;
};

struct ADLODNPerformanceLevelInternal {
    int iClock;
    int iVddc;
    int iEnabled;
};

struct ADLODNPerformanceLevelsInternal {
    int iSize;
    int iMode;
    int iNumberOfPerformanceLevels;
    ADLODNPerformanceLevelInternal aLevels[16];
};

// ADL e __stdcall no Win32. Sem o convention no typedef, o x64 compila (la so existe uma
// convencao) e o x86 recusa o endereco da callback com C2664.
typedef void* (__stdcall *ADL_MAIN_MALLOC_CALLBACK)(int);
typedef int (*ADL_Main_Control_Create_t)(ADL_MAIN_MALLOC_CALLBACK, int);
typedef int (*ADL_Main_Control_Destroy_t)();
typedef int (*ADL_Adapter_NumberOfAdapters_Get_t)(int*);
typedef int (*ADL_Adapter_AdapterInfo_Get_t)(ADLAdapterInfoInternal*, int);
typedef int (*ADL_Overdrive5_Temperature_Get_t)(int, int, ADLTemperatureInternal*);
typedef int (*ADL_Overdrive5_CurrentActivity_Get_t)(int, ADLPMActivityInternal*);
typedef int (*ADL2_OverdriveN_PowerLimit_Set_t)(void*, int, int);
typedef int (*ADL2_OverdriveN_PowerLimit_Get_t)(void*, int, int*, int*);
typedef int (*ADL2_OverdriveN_PerformanceLevels_Set_t)(void*, int, ADLODNPerformanceLevelsInternal*);

static void* __stdcall AdlMemoryAlloc(int size) {
    return malloc(size);
}

// ============================================================================
// 3. Definições e Assinaturas Intel IGCL (Intel)
// ============================================================================
#define CTL_MAKE_VERSION(major, minor) ((major << 16) | minor)
#define CTL_RESULT_SUCCESS 0

typedef void* ctl_api_handle_t;
typedef void* ctl_device_adapter_handle_t;

struct ctl_init_args_t {
    uint32_t Size;
    uint8_t Version;
    uint32_t AppVersion;
    uint32_t flags;
    uint32_t SupportedVersion;
};

struct ctl_device_adapter_properties_t {
    uint32_t Size;
    uint8_t Version;
    void* pNext;
    uint32_t device_type;
    uint32_t supported_subslice_count;
    uint64_t supported_slice_mask;
    uint32_t pci_vendor_id;
    uint32_t pci_device_id;
    uint32_t rev_id;
    uint32_t num_slices;
    uint32_t num_subslices_per_slice;
    char name[256];
};

struct ctl_oc_telemetry_t {
    uint32_t Size;
    uint8_t Version;
    double gpuVoltage;
    double gpuCurrent;
    double gpuTemperature;
    double gpuFrequency;
    double gpuPowerWatts;
};

struct ctl_oc_properties_t {
    uint32_t Size;
    uint8_t Version;
    uint8_t bSupported;
};

typedef int (*ctl_pfnInit_t)(ctl_init_args_t*, ctl_api_handle_t*);
typedef int (*ctl_pfnClose_t)(ctl_api_handle_t);
typedef int (*ctl_pfnEnumerateDevices_t)(ctl_api_handle_t, uint32_t*, ctl_device_adapter_handle_t*);
typedef int (*ctl_pfnGetDeviceProperties_t)(ctl_device_adapter_handle_t, ctl_device_adapter_properties_t*);
typedef int (*ctl_pfnOverclockWaiverSet_t)(ctl_device_adapter_handle_t);
typedef int (*ctl_pfnOverclockGpuFrequencyOffsetSet_t)(ctl_device_adapter_handle_t, double);
typedef int (*ctl_pfnOverclockPowerLimitSet_t)(ctl_device_adapter_handle_t, double);
typedef int (*ctl_pfnPowerTelemetryGet_t)(ctl_device_adapter_handle_t, ctl_oc_telemetry_t*);

// ============================================================================
// Ponteiros Globais Dinâmicos
// ============================================================================
// NVIDIA
static NvAPI_QueryInterface_t g_nvQueryInterface = nullptr;
static NvAPI_Initialize_t g_nvInit = nullptr;
static NvAPI_Unload_t g_nvUnload = nullptr;
static NvAPI_EnumPhysicalGPUs_t g_nvEnumPhysicalGPUs = nullptr;
static NvAPI_GPU_GetFullName_t g_nvGetFullName = nullptr;
static NvAPI_GPU_GetThermalSettings_t g_nvGetThermal = nullptr;
static NvAPI_GPU_GetAllClockFrequencies_t g_nvGetAllClocks = nullptr;
static NvAPI_GPU_GetDynamicPstatesInfoEx_t g_nvGetDynPstates = nullptr;
static NvAPI_GPU_GetPstates20_t g_nvGetPstates20 = nullptr;
static NvAPI_GPU_SetPstates20_t g_nvSetPstates20 = nullptr;
static NvAPI_GPU_ClientPowerPoliciesSetStatus_t g_nvSetPowerPolicies = nullptr;
static NvAPI_GPU_ClientPowerTopologySetStatus_t g_nvSetPowerTopology = nullptr;

// AMD
static ADL_Main_Control_Create_t g_adlCreate = nullptr;
static ADL_Main_Control_Destroy_t g_adlDestroy = nullptr;
static ADL_Adapter_NumberOfAdapters_Get_t g_adlNumAdapters = nullptr;
static ADL_Adapter_AdapterInfo_Get_t g_adlAdapterInfo = nullptr;
static ADL_Overdrive5_Temperature_Get_t g_adlTempGet = nullptr;
static ADL_Overdrive5_CurrentActivity_Get_t g_adlActivityGet = nullptr;
static ADL2_OverdriveN_PowerLimit_Set_t g_adlPowerLimitSet = nullptr;

// Intel
static ctl_pfnInit_t g_intelInit = nullptr;
static ctl_pfnClose_t g_intelClose = nullptr;
static ctl_pfnEnumerateDevices_t g_intelEnumDevices = nullptr;
static ctl_pfnGetDeviceProperties_t g_intelGetProps = nullptr;
static ctl_pfnOverclockWaiverSet_t g_intelOcWaiver = nullptr;
static ctl_pfnOverclockGpuFrequencyOffsetSet_t g_intelOcFreqOffsetSet = nullptr;
static ctl_pfnOverclockPowerLimitSet_t g_intelOcPowerLimitSet = nullptr;
static ctl_pfnPowerTelemetryGet_t g_intelPowerTelemetry = nullptr;

// ============================================================================
// Implementação de GpuControlManager
// ============================================================================
GpuControlManager& GpuControlManager::Instance() {
    static GpuControlManager instance;
    return instance;
}

GpuControlManager::GpuControlManager() = default;

GpuControlManager::~GpuControlManager() {
    if (m_nvapiLib) {
        if (g_nvUnload) g_nvUnload();
        FreeLibrary(reinterpret_cast<HMODULE>(m_nvapiLib));
        m_nvapiLib = nullptr;
    }
    if (m_adlLib) {
        if (g_adlDestroy) g_adlDestroy();
        FreeLibrary(reinterpret_cast<HMODULE>(m_adlLib));
        m_adlLib = nullptr;
    }
    if (m_adlxLib) {
        FreeLibrary(reinterpret_cast<HMODULE>(m_adlxLib));
        m_adlxLib = nullptr;
    }
    if (m_igclLib) {
        FreeLibrary(reinterpret_cast<HMODULE>(m_igclLib));
        m_igclLib = nullptr;
    }
}

bool GpuControlManager::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    DetectAdapters();
    m_initialized = true;
    return true;
}

size_t GpuControlManager::GetGpuCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hasMock) return 1;
    return m_gpus.size();
}

GpuControlCapabilities GpuControlManager::GetCapabilities(size_t gpuIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hasMock) return m_mockCaps;
    if (gpuIndex < m_gpus.size()) return m_gpus[gpuIndex];
    return GpuControlCapabilities{};
}

GpuControlStatus GpuControlManager::GetStatus(size_t gpuIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hasMock) return m_mockStatus;
    if (gpuIndex < m_statuses.size()) {
        auto vendor = m_gpus[gpuIndex].vendor;
        if (vendor == GpuVendor::Nvidia) {
            UpdateNvidiaTelemetry(gpuIndex);
        } else if (vendor == GpuVendor::Amd) {
            UpdateAmdTelemetry(gpuIndex);
        } else if (vendor == GpuVendor::Intel) {
            UpdateIntelTelemetry(gpuIndex);
        }
        return m_statuses[gpuIndex];
    }
    return GpuControlStatus{};
}

void GpuControlManager::DetectAdapters() {
    m_gpus.clear();
    m_statuses.clear();
    m_hwContexts.clear();

    auto detected = GpuDetector::DetectGpus();
    for (const auto& d : detected) {
        GpuControlCapabilities caps;
        caps.adapterName = d.description;
        caps.vendorId = d.vendorId;
        caps.deviceId = d.deviceId;

        if (d.vendorId == 0x10DE) {
            caps.vendor = GpuVendor::Nvidia;
        } else if (d.vendorId == 0x1002) {
            caps.vendor = GpuVendor::Amd;
        } else if (d.vendorId == 0x8086) {
            caps.vendor = GpuVendor::Intel;
        } else {
            caps.vendor = GpuVendor::Unknown;
        }

        m_gpus.push_back(caps);
        m_statuses.push_back(GpuControlStatus{});
        m_hwContexts.push_back(GpuHardwareContext{});
    }

    // Inicialização dos provedores por fabricante
    InitNvidiaNvapi();
    InitAmdAdlx();
    InitIntelIgcl();

    for (size_t i = 0; i < m_gpus.size(); ++i) {
        QueryVendorCapabilities(i);
    }
}

// ============================================================================
// FASE 1: Provedor NVIDIA NVAPI
// ============================================================================
bool GpuControlManager::InitNvidiaNvapi() {
    if (m_nvapiLib) return true;

    // O NVAPI de 32 bits chama-se nvapi.dll; nvapi64.dll so existe para processos de 64 bits.
    // LOAD_LIBRARY_SEARCH_SYSTEM32 precisa da KB253648 no Windows 7, por isso ha fallback para
    // o LoadLibrary comum antes de desistir do provedor.
    const wchar_t* nvApiNames[] = {
#if defined(_WIN64)
        L"nvapi64.dll", L"nvapi.dll"
#else
        L"nvapi.dll", L"nvapi64.dll"
#endif
    };
    HMODULE hNv = nullptr;
    for (const wchar_t* name : nvApiNames) {
        hNv = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!hNv) hNv = LoadLibraryW(name);
        if (hNv) break;
    }
    if (!hNv) return false;

    g_nvQueryInterface = reinterpret_cast<NvAPI_QueryInterface_t>(GetProcAddress(hNv, "nvapi_QueryInterface"));
    if (!g_nvQueryInterface) {
        FreeLibrary(hNv);
        return false;
    }

    g_nvInit = reinterpret_cast<NvAPI_Initialize_t>(g_nvQueryInterface(0x0150E828));
    if (!g_nvInit || g_nvInit() != 0) {
        FreeLibrary(hNv);
        return false;
    }

    m_nvapiLib = hNv;
    g_nvUnload = reinterpret_cast<NvAPI_Unload_t>(g_nvQueryInterface(0xD22BDD7E));
    g_nvEnumPhysicalGPUs = reinterpret_cast<NvAPI_EnumPhysicalGPUs_t>(g_nvQueryInterface(0xE5AC211A));
    g_nvGetFullName = reinterpret_cast<NvAPI_GPU_GetFullName_t>(g_nvQueryInterface(0xCEEE8E9F));
    g_nvGetThermal = reinterpret_cast<NvAPI_GPU_GetThermalSettings_t>(g_nvQueryInterface(0xE7EEA562));
    g_nvGetAllClocks = reinterpret_cast<NvAPI_GPU_GetAllClockFrequencies_t>(g_nvQueryInterface(0xDCB616C3));
    g_nvGetDynPstates = reinterpret_cast<NvAPI_GPU_GetDynamicPstatesInfoEx_t>(g_nvQueryInterface(0x60DED2ED));
    g_nvGetPstates20 = reinterpret_cast<NvAPI_GPU_GetPstates20_t>(g_nvQueryInterface(0x6FF81213));
    g_nvSetPstates20 = reinterpret_cast<NvAPI_GPU_SetPstates20_t>(g_nvQueryInterface(0x0F4DAE6B));
    g_nvSetPowerPolicies = reinterpret_cast<NvAPI_GPU_ClientPowerPoliciesSetStatus_t>(g_nvQueryInterface(0xAD95F5DD));
    g_nvSetPowerTopology = reinterpret_cast<NvAPI_GPU_ClientPowerTopologySetStatus_t>(g_nvQueryInterface(0xA4E753B7));

    // Associa cada GPU física aos adaptadores detectados
    if (g_nvEnumPhysicalGPUs) {
        void* handles[64] = { nullptr };
        int count = 0;
        if (g_nvEnumPhysicalGPUs(handles, &count) == 0 && count > 0) {
            int nvIdx = 0;
            for (size_t i = 0; i < m_gpus.size(); ++i) {
                if (m_gpus[i].vendor == GpuVendor::Nvidia && nvIdx < count) {
                    m_hwContexts[i].nativeHandle = handles[nvIdx++];
                }
            }
        }
    }

    OPIT_LOG_INFO("[GPU] Provedor NVIDIA NVAPI carregado e configurado com sucesso.");
    return true;
}

// ============================================================================
// FASE 2: Provedor AMD ADL / ADLX
// ============================================================================
bool GpuControlManager::InitAmdAdlx() {
    if (m_adlLib) return true;

    HMODULE hAdl = LoadLibraryW(L"atiadlxx.dll");
    if (!hAdl) {
        hAdl = LoadLibraryW(L"atiadlxy.dll");
    }
    if (!hAdl) return false;

    g_adlCreate = (ADL_Main_Control_Create_t)GetProcAddress(hAdl, "ADL_Main_Control_Create");
    g_adlDestroy = (ADL_Main_Control_Destroy_t)GetProcAddress(hAdl, "ADL_Main_Control_Destroy");
    g_adlNumAdapters = (ADL_Adapter_NumberOfAdapters_Get_t)GetProcAddress(hAdl, "ADL_Adapter_NumberOfAdapters_Get");
    g_adlAdapterInfo = (ADL_Adapter_AdapterInfo_Get_t)GetProcAddress(hAdl, "ADL_Adapter_AdapterInfo_Get");
    g_adlTempGet = (ADL_Overdrive5_Temperature_Get_t)GetProcAddress(hAdl, "ADL_Overdrive5_Temperature_Get");
    g_adlActivityGet = (ADL_Overdrive5_CurrentActivity_Get_t)GetProcAddress(hAdl, "ADL_Overdrive5_CurrentActivity_Get");
    g_adlPowerLimitSet = (ADL2_OverdriveN_PowerLimit_Set_t)GetProcAddress(hAdl, "ADL2_OverdriveN_PowerLimit_Set");

    if (g_adlCreate && g_adlCreate(AdlMemoryAlloc, 1) == 0) {
        m_adlLib = hAdl;

        if (g_adlNumAdapters && g_adlAdapterInfo) {
            int numAdapters = 0;
            if (g_adlNumAdapters(&numAdapters) == 0 && numAdapters > 0) {
                std::vector<ADLAdapterInfoInternal> adpInfos(numAdapters);
                if (g_adlAdapterInfo(adpInfos.data(), sizeof(ADLAdapterInfoInternal) * numAdapters) == 0) {
                    for (int a = 0; a < numAdapters; ++a) {
                        if (adpInfos[a].iVendorID == 0x1002 && adpInfos[a].iPresent) {
                            for (size_t i = 0; i < m_gpus.size(); ++i) {
                                if (m_gpus[i].vendor == GpuVendor::Amd && m_hwContexts[i].adlAdapterIndex < 0) {
                                    m_hwContexts[i].adlAdapterIndex = adpInfos[a].iAdapterIndex;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        OPIT_LOG_INFO("[GPU] Provedor AMD ADL inicializado com sucesso.");
        return true;
    }

    FreeLibrary(hAdl);
    return false;
}

// ============================================================================
// FASE 3: Provedor Intel IGCL
// ============================================================================
bool GpuControlManager::InitIntelIgcl() {
    if (m_igclLib) return true;

    HMODULE hCtl = LoadLibraryW(L"ControlLib.dll");
    if (!hCtl) {
        hCtl = LoadLibraryW(L"ControlLib64.dll");
    }
    if (!hCtl) return false;

    g_intelInit = (ctl_pfnInit_t)GetProcAddress(hCtl, "ctlInit");
    g_intelClose = (ctl_pfnClose_t)GetProcAddress(hCtl, "ctlClose");
    g_intelEnumDevices = (ctl_pfnEnumerateDevices_t)GetProcAddress(hCtl, "ctlEnumerateDevices");
    g_intelGetProps = (ctl_pfnGetDeviceProperties_t)GetProcAddress(hCtl, "ctlGetDeviceProperties");
    g_intelOcWaiver = (ctl_pfnOverclockWaiverSet_t)GetProcAddress(hCtl, "ctlOverclockWaiverSet");
    g_intelOcFreqOffsetSet = (ctl_pfnOverclockGpuFrequencyOffsetSet_t)GetProcAddress(hCtl, "ctlOverclockGpuFrequencyOffsetSet");
    g_intelOcPowerLimitSet = (ctl_pfnOverclockPowerLimitSet_t)GetProcAddress(hCtl, "ctlOverclockPowerLimitSet");
    g_intelPowerTelemetry = (ctl_pfnPowerTelemetryGet_t)GetProcAddress(hCtl, "ctlPowerTelemetryGet");

    if (g_intelInit) {
        ctl_init_args_t initArgs{};
        initArgs.Size = sizeof(ctl_init_args_t);
        initArgs.Version = 0;
        initArgs.AppVersion = CTL_MAKE_VERSION(1, 0);

        ctl_api_handle_t hApi = nullptr;
        if (g_intelInit(&initArgs, &hApi) == CTL_RESULT_SUCCESS && hApi) {
            m_igclLib = hCtl;

            if (g_intelEnumDevices) {
                uint32_t devCount = 0;
                if (g_intelEnumDevices(hApi, &devCount, nullptr) == CTL_RESULT_SUCCESS && devCount > 0) {
                    std::vector<ctl_device_adapter_handle_t> devices(devCount);
                    if (g_intelEnumDevices(hApi, &devCount, devices.data()) == CTL_RESULT_SUCCESS) {
                        for (uint32_t d = 0; d < devCount; ++d) {
                            ctl_device_adapter_properties_t props{};
                            props.Size = sizeof(ctl_device_adapter_properties_t);
                            if (g_intelGetProps && g_intelGetProps(devices[d], &props) == CTL_RESULT_SUCCESS) {
                                for (size_t i = 0; i < m_gpus.size(); ++i) {
                                    if (m_gpus[i].vendor == GpuVendor::Intel && m_hwContexts[i].nativeHandle == nullptr) {
                                        m_hwContexts[i].nativeHandle = devices[d];
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            OPIT_LOG_INFO("[GPU] Provedor Intel IGCL inicializado com sucesso.");
            return true;
        }
    }

    FreeLibrary(hCtl);
    return false;
}

// ============================================================================
// Consulta de Recursos por Fabricante
// ============================================================================
void GpuControlManager::QueryVendorCapabilities(size_t index) {
    if (index >= m_gpus.size()) return;
    auto& caps = m_gpus[index];
    auto& ctx = m_hwContexts[index];

    switch (caps.vendor) {
    case GpuVendor::Nvidia: {
        if (m_nvapiLib && ctx.nativeHandle) {
            caps.supported = true;
            caps.canControlPowerLimit = (g_nvSetPowerPolicies != nullptr || g_nvSetPowerTopology != nullptr);
            caps.minPowerLimitPercent = 60;
            caps.maxPowerLimitPercent = 115;
            caps.defaultPowerLimitPercent = 100;

            caps.canControlCoreClock = (g_nvSetPstates20 != nullptr);
            caps.minCoreClockOffsetMHz = -200;
            caps.maxCoreClockOffsetMHz = 200;

            caps.canControlMemoryClock = (g_nvSetPstates20 != nullptr);
            caps.minMemoryClockOffsetMHz = -200;
            caps.maxMemoryClockOffsetMHz = 400;

            ctx.canPstates = caps.canControlCoreClock;
            ctx.canPower = caps.canControlPowerLimit;
            caps.limitationReason = "Driver NVIDIA NVAPI ativo. Controles restritos aos limites seguros da GPU.";
            return;
        }
        caps.supported = false;
        caps.limitationReason = "NVAPI indisponivel no driver instalado. Telemetria DXGI preservada.";
        break;
    }
    case GpuVendor::Amd: {
        if (m_adlLib && ctx.adlAdapterIndex >= 0) {
            caps.supported = true;
            caps.canControlPowerLimit = (g_adlPowerLimitSet != nullptr);
            caps.minPowerLimitPercent = 70;
            caps.maxPowerLimitPercent = 115;
            caps.defaultPowerLimitPercent = 100;

            caps.canControlCoreClock = true;
            caps.minCoreClockOffsetMHz = -200;
            caps.maxCoreClockOffsetMHz = 250;

            caps.canControlMemoryClock = true;
            caps.minMemoryClockOffsetMHz = -150;
            caps.maxMemoryClockOffsetMHz = 300;

            caps.limitationReason = "Driver AMD ADL/Overdrive ativo. Controles sincronizados ao driver Radeon.";
            return;
        }
        caps.supported = false;
        caps.limitationReason = "Driver AMD ADL/ADLX nao carregado no ambiente corrente.";
        break;
    }
    case GpuVendor::Intel: {
        if (m_igclLib && ctx.nativeHandle) {
            caps.supported = true;
            caps.canControlPowerLimit = (g_intelOcPowerLimitSet != nullptr);
            caps.minPowerLimitPercent = 70;
            caps.maxPowerLimitPercent = 120;
            caps.defaultPowerLimitPercent = 100;

            caps.canControlCoreClock = (g_intelOcFreqOffsetSet != nullptr);
            caps.minCoreClockOffsetMHz = -150;
            caps.maxCoreClockOffsetMHz = 200;

            caps.canControlMemoryClock = false; // Intel Arc foca na frequência do silício gráfico
            caps.limitationReason = "Intel Graphics Control Library (IGCL) ativa para Intel Arc/Iris.";
            return;
        }
        caps.supported = false;
        caps.limitationReason = "Intel Graphics Control Library (IGCL) nao presente no sistema.";
        break;
    }
    default:
        caps.supported = false;
        caps.limitationReason = "Fabricante de GPU nao suporta controle direto de clock por API publica.";
        break;
    }
}

// ============================================================================
// Aplicação Específica: NVIDIA
// ============================================================================
bool GpuControlManager::ApplyNvidia(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg) {
    if (gpuIndex >= m_hwContexts.size() || !m_hwContexts[gpuIndex].nativeHandle) {
        outMsg = "Handle físico da GPU NVIDIA indisponível.";
        return false;
    }

    void* hGpu = m_hwContexts[gpuIndex].nativeHandle;
    std::string details;
    bool ok = true;

    // 1. Clocks PStates20
    if (g_nvSetPstates20 && (coreOffsetMHz != 0 || memOffsetMHz != 0 || m_hwContexts[gpuIndex].canPstates)) {
        NV_GPU_PERF_PSTATES20_INFO_INT setInfo{};
        setInfo.version = (unsigned int)(sizeof(NV_GPU_PERF_PSTATES20_INFO_INT) | 0x10000);
        setInfo.numPstates = 1;
        setInfo.numClocks = 2;
        setInfo.pstates[0].pstateId = 0; // P0 State (Performance Máxima)

        setInfo.pstates[0].clocks[0].domainId = 0; // Core
        setInfo.pstates[0].clocks[0].freqDelta_kHz.value = coreOffsetMHz * 1000;

        setInfo.pstates[0].clocks[1].domainId = 4; // Memory
        setInfo.pstates[0].clocks[1].freqDelta_kHz.value = memOffsetMHz * 1000;

        int status = g_nvSetPstates20(hGpu, &setInfo);
        if (status == 0) {
            details += "Clocks NVIDIA aplicados (Core: " + std::to_string(coreOffsetMHz) + "MHz, Mem: " + std::to_string(memOffsetMHz) + "MHz). ";
        } else {
            ok = false;
            details += "Driver NVIDIA rejeitou offset de frequencia (cod: " + std::to_string(status) + "). ";
        }
    }

    // 2. Power Limit
    if (powerLimitPercent > 0) {
        bool pwrOk = false;
        if (g_nvSetPowerPolicies) {
            NV_GPU_POWER_POLICIES_STATUS_INT pwrStat{};
            pwrStat.version = (unsigned int)(sizeof(NV_GPU_POWER_POLICIES_STATUS_INT) | 0x10000);
            pwrStat.count = 1;
            pwrStat.entries[0].performanceStateId = 0;
            pwrStat.entries[0].powerPolicy = (unsigned int)(powerLimitPercent * 1000);

            if (g_nvSetPowerPolicies(hGpu, &pwrStat) == 0) {
                pwrOk = true;
                details += "Power Limit NVIDIA ajustado para " + std::to_string(powerLimitPercent) + "%. ";
            }
        }
        if (!pwrOk && g_nvSetPowerTopology) {
            NV_GPU_POWER_TOPOLOGY_STATUS_INT topoStat{};
            topoStat.version = (unsigned int)(sizeof(NV_GPU_POWER_TOPOLOGY_STATUS_INT) | 0x10000);
            topoStat.count = 1;
            topoStat.entries[0].domain = 0;
            topoStat.entries[0].power = (unsigned int)(powerLimitPercent * 1000);

            if (g_nvSetPowerTopology(hGpu, &topoStat) == 0) {
                pwrOk = true;
                details += "Power Topology NVIDIA ajustado para " + std::to_string(powerLimitPercent) + "%. ";
            }
        }
        if (!pwrOk) {
            ok = false;
            details += "Power limit " + std::to_string(powerLimitPercent) +
                       "% nao foi aceito pelo driver NVIDIA (sem interface ou rejeitado). ";
        }
    }

    outMsg = details.empty() ? "Nada a aplicar no NVIDIA (valores ja no padrao do driver)." : details;
    return ok;
}

// ============================================================================
// Aplicação Específica: AMD
// ============================================================================
bool GpuControlManager::ApplyAmd(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg) {
    if (gpuIndex >= m_hwContexts.size() || m_hwContexts[gpuIndex].adlAdapterIndex < 0) {
        outMsg = "Índice de adaptador AMD ADL indisponível.";
        return false;
    }

    int adapterIdx = m_hwContexts[gpuIndex].adlAdapterIndex;
    std::string details;
    bool ok = true;

    // Power Limit
    if (powerLimitPercent > 0) {
        if (g_adlPowerLimitSet) {
            // No ADL OverdriveN, o valor é delta percentual em relação a 100% (ex: -20 a +15)
            int deltaPwr = powerLimitPercent - 100;
            int res = g_adlPowerLimitSet(nullptr, adapterIdx, deltaPwr);
            if (res == 0) {
                details += "Power Limit AMD ajustado para " + std::to_string(powerLimitPercent) + "%. ";
            } else {
                ok = false;
                details += "Driver AMD rejeitou power limit (cod: " + std::to_string(res) + "). ";
            }
        } else {
            ok = false;
            details += "Interface ADL de power limit indisponivel; nada foi escrito no driver. ";
        }
    }

    // Honestidade: esta versao nao tem interface ADL para escrever offsets de clock. Se a pessoa
    // pediu offset, o resultado tem que dizer que o clock NAO foi aplicado em vez de fingir.
    if (coreOffsetMHz != 0 || memOffsetMHz != 0) {
        ok = false;
        details += "Offsets de clock AMD nao aplicaveis nesta versao (sem interface ADL de clocks); "
                   "apenas o power limit e escrito. ";
    }

    outMsg = details.empty() ? "Nada a aplicar no AMD (valores ja no padrao do driver)." : details;
    return ok;
}

// ============================================================================
// Aplicação Específica: Intel
// ============================================================================
bool GpuControlManager::ApplyIntel(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg) {
    if (gpuIndex >= m_hwContexts.size() || !m_hwContexts[gpuIndex].nativeHandle) {
        outMsg = "Handle de dispositivo Intel Arc indisponível.";
        return false;
    }

    ctl_device_adapter_handle_t hDev = (ctl_device_adapter_handle_t)m_hwContexts[gpuIndex].nativeHandle;
    std::string details;
    bool ok = true;

    // Libera waiver de overclock se necessário
    if (g_intelOcWaiver) {
        g_intelOcWaiver(hDev);
    }

    // Offset de frequência
    if (g_intelOcFreqOffsetSet) {
        int res = g_intelOcFreqOffsetSet(hDev, (double)coreOffsetMHz);
        if (res == CTL_RESULT_SUCCESS) {
            details += "Offset de frequencia Intel aplicado (" + std::to_string(coreOffsetMHz) + " MHz). ";
        } else {
            ok = false;
            details += "Driver Intel rejeitou offset de frequencia (cod: " + std::to_string(res) + "). ";
        }
    } else if (coreOffsetMHz != 0) {
        ok = false;
        details += "Interface IGCL de offset de frequencia indisponivel; nada foi escrito. ";
    }

    // Power Limit
    if (g_intelOcPowerLimitSet && powerLimitPercent > 0) {
        double powerWatts = (double)(powerLimitPercent * 1.5); // Conversão heurística para faixa de TDP Arc
        int res = g_intelOcPowerLimitSet(hDev, powerWatts);
        if (res == CTL_RESULT_SUCCESS) {
            details += "Power Limit Intel ajustado. ";
        } else {
            ok = false;
            details += "Driver Intel rejeitou power limit (cod: " + std::to_string(res) + "). ";
        }
    } else if (powerLimitPercent > 0) {
        ok = false;
        details += "Interface IGCL de power limit indisponivel; nada foi escrito. ";
    }

    outMsg = details.empty() ? "Nada a aplicar no Intel (valores ja no padrao do driver)." : details;
    return ok;
}

// ============================================================================
// Telemetria em Tempo Real
// ============================================================================
void GpuControlManager::UpdateNvidiaTelemetry(size_t gpuIndex) {
    if (gpuIndex >= m_hwContexts.size() || !m_hwContexts[gpuIndex].nativeHandle) return;
    void* hGpu = m_hwContexts[gpuIndex].nativeHandle;
    auto& stat = m_statuses[gpuIndex];

    if (g_nvGetThermal) {
        NV_GPU_THERMAL_SETTINGS_INTERNAL thermal{};
        thermal.version = (unsigned int)(sizeof(NV_GPU_THERMAL_SETTINGS_INTERNAL) | 0x20000);
        if (g_nvGetThermal(hGpu, 0, &thermal) == 0 && thermal.count > 0) {
            stat.measuredTemperatureC = (double)thermal.sensor[0].currentTemp;
        }
    }

    if (g_nvGetAllClocks) {
        NV_GPU_CLOCK_FREQUENCIES_INTERNAL clks{};
        clks.version = (unsigned int)(sizeof(NV_GPU_CLOCK_FREQUENCIES_INTERNAL) | 0x30000);
        clks.ClockType = 0; // Current
        if (g_nvGetAllClocks(hGpu, &clks) == 0) {
            if (clks.domain[0].bIsPresent) {
                stat.measuredCoreClockMHz = (int)(clks.domain[0].frequency / 1000);
            }
            if (clks.domain[4].bIsPresent) {
                stat.measuredMemoryClockMHz = (int)(clks.domain[4].frequency / 1000);
            }
        }
    }

    if (g_nvGetDynPstates) {
        NV_GPU_DYNAMIC_PSTATES_INFO_EX_INTERNAL dyn{};
        dyn.version = (unsigned int)(sizeof(NV_GPU_DYNAMIC_PSTATES_INFO_EX_INTERNAL) | 0x10000);
        if (g_nvGetDynPstates(hGpu, &dyn) == 0 && dyn.utilization[0].bIsPresent) {
            stat.measuredUsagePercent = (int)dyn.utilization[0].percentage;
        }
    }
}

void GpuControlManager::UpdateAmdTelemetry(size_t gpuIndex) {
    if (gpuIndex >= m_hwContexts.size() || m_hwContexts[gpuIndex].adlAdapterIndex < 0) return;
    int adapterIdx = m_hwContexts[gpuIndex].adlAdapterIndex;
    auto& stat = m_statuses[gpuIndex];

    if (g_adlTempGet) {
        ADLTemperatureInternal temp{};
        temp.iSize = sizeof(ADLTemperatureInternal);
        if (g_adlTempGet(adapterIdx, 0, &temp) == 0) {
            stat.measuredTemperatureC = (double)(temp.iTemperature / 1000.0);
        }
    }

    if (g_adlActivityGet) {
        ADLPMActivityInternal act{};
        act.iSize = sizeof(ADLPMActivityInternal);
        if (g_adlActivityGet(adapterIdx, &act) == 0) {
            stat.measuredCoreClockMHz = act.iEngineClock / 100;
            stat.measuredMemoryClockMHz = act.iMemoryClock / 100;
            stat.measuredUsagePercent = act.iActivityPercent;
        }
    }
}

void GpuControlManager::UpdateIntelTelemetry(size_t gpuIndex) {
    if (gpuIndex >= m_hwContexts.size() || !m_hwContexts[gpuIndex].nativeHandle) return;
    ctl_device_adapter_handle_t hDev = (ctl_device_adapter_handle_t)m_hwContexts[gpuIndex].nativeHandle;
    auto& stat = m_statuses[gpuIndex];

    if (g_intelPowerTelemetry) {
        ctl_oc_telemetry_t telem{};
        telem.Size = sizeof(ctl_oc_telemetry_t);
        if (g_intelPowerTelemetry(hDev, &telem) == CTL_RESULT_SUCCESS) {
            stat.measuredTemperatureC = telem.gpuTemperature;
            stat.measuredCoreClockMHz = (int)telem.gpuFrequency;
            stat.measuredPowerWatts = (int)telem.gpuPowerWatts;
        }
    }
}

// ============================================================================
// Aplicação de Perfis e Custom Settings
// ============================================================================
bool GpuControlManager::ApplyProfile(size_t gpuIndex, GpuPowerProfile profile, std::string& outMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    GpuControlCapabilities caps;
    GpuControlStatus* pStatus = nullptr;

    if (m_hasMock) {
        caps = m_mockCaps;
        pStatus = &m_mockStatus;
    } else {
        if (gpuIndex >= m_gpus.size()) {
            outMsg = "Indice de GPU invalido.";
            return false;
        }
        caps = m_gpus[gpuIndex];
        pStatus = &m_statuses[gpuIndex];
    }

    pStatus->currentProfile = profile;

    switch (profile) {
    case GpuPowerProfile::Eco:
        pStatus->requestedPowerLimitPercent = caps.minPowerLimitPercent;
        pStatus->requestedCoreClockOffsetMHz = -50;
        pStatus->requestedMemoryClockOffsetMHz = 0;
        break;
    case GpuPowerProfile::Balanced:
        pStatus->requestedPowerLimitPercent = caps.defaultPowerLimitPercent;
        pStatus->requestedCoreClockOffsetMHz = 0;
        pStatus->requestedMemoryClockOffsetMHz = 0;
        break;
    case GpuPowerProfile::HighPerformance:
        pStatus->requestedPowerLimitPercent = caps.maxPowerLimitPercent;
        pStatus->requestedCoreClockOffsetMHz = 50;
        pStatus->requestedMemoryClockOffsetMHz = 100;
        break;
    case GpuPowerProfile::Custom:
        break;
    }

    if (!caps.supported && !m_hasMock) {
        pStatus->appliedPowerLimitPercent = caps.defaultPowerLimitPercent;
        pStatus->appliedCoreClockOffsetMHz = 0;
        pStatus->appliedMemoryClockOffsetMHz = 0;
        outMsg = "Perfil " + std::string(GpuPowerProfileToString(profile)) +
                 " registrado, mas o hardware nao permite alteracao direta de clocks (" + caps.limitationReason + ").";
        return true;
    }

    // Se tiver mock ativo, atualiza imediatamente para testes determinísticos
    if (m_hasMock) {
        pStatus->appliedPowerLimitPercent = pStatus->requestedPowerLimitPercent;
        pStatus->appliedCoreClockOffsetMHz = pStatus->requestedCoreClockOffsetMHz;
        pStatus->appliedMemoryClockOffsetMHz = pStatus->requestedMemoryClockOffsetMHz;
        outMsg = "Perfil " + std::string(GpuPowerProfileToString(profile)) + " aplicado com sucesso.";
        return true;
    }

    // Aplicação real no hardware por fabricante
    std::string hardwareMsg;
    bool ok = false;
    if (caps.vendor == GpuVendor::Nvidia) {
        ok = ApplyNvidia(gpuIndex, pStatus->requestedCoreClockOffsetMHz, pStatus->requestedMemoryClockOffsetMHz, pStatus->requestedPowerLimitPercent, hardwareMsg);
    } else if (caps.vendor == GpuVendor::Amd) {
        ok = ApplyAmd(gpuIndex, pStatus->requestedCoreClockOffsetMHz, pStatus->requestedMemoryClockOffsetMHz, pStatus->requestedPowerLimitPercent, hardwareMsg);
    } else if (caps.vendor == GpuVendor::Intel) {
        ok = ApplyIntel(gpuIndex, pStatus->requestedCoreClockOffsetMHz, pStatus->requestedMemoryClockOffsetMHz, pStatus->requestedPowerLimitPercent, hardwareMsg);
    }

    if (!ok) {
        // O driver nao aceitou: os valores "aplicados" permanecem os anteriores e a funcao
        // reporta falha em vez de fingir sucesso.
        outMsg = "Perfil " + std::string(GpuPowerProfileToString(profile)) + " NAO aplicado no hardware. " + hardwareMsg;
        return false;
    }

    pStatus->appliedPowerLimitPercent = pStatus->requestedPowerLimitPercent;
    pStatus->appliedCoreClockOffsetMHz = pStatus->requestedCoreClockOffsetMHz;
    pStatus->appliedMemoryClockOffsetMHz = pStatus->requestedMemoryClockOffsetMHz;

    outMsg = "Perfil " + std::string(GpuPowerProfileToString(profile)) + " aplicado. " + hardwareMsg;
    return true;
}

bool GpuControlManager::ApplyCustomSettings(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    GpuControlCapabilities caps;
    GpuControlStatus* pStatus = nullptr;

    if (m_hasMock) {
        caps = m_mockCaps;
        pStatus = &m_mockStatus;
    } else {
        if (gpuIndex >= m_gpus.size()) {
            outMsg = "Indice de GPU invalido.";
            return false;
        }
        caps = m_gpus[gpuIndex];
        pStatus = &m_statuses[gpuIndex];
    }

    // Clampa valores aos limites aceitos de seguranca da placa
    coreOffsetMHz = std::clamp(coreOffsetMHz, caps.minCoreClockOffsetMHz, caps.maxCoreClockOffsetMHz);
    memOffsetMHz = std::clamp(memOffsetMHz, caps.minMemoryClockOffsetMHz, caps.maxMemoryClockOffsetMHz);
    powerLimitPercent = std::clamp(powerLimitPercent, caps.minPowerLimitPercent, caps.maxPowerLimitPercent);

    pStatus->currentProfile = GpuPowerProfile::Custom;
    pStatus->requestedCoreClockOffsetMHz = coreOffsetMHz;
    pStatus->requestedMemoryClockOffsetMHz = memOffsetMHz;
    pStatus->requestedPowerLimitPercent = powerLimitPercent;

    if (!caps.supported && !m_hasMock) {
        pStatus->appliedCoreClockOffsetMHz = 0;
        pStatus->appliedMemoryClockOffsetMHz = 0;
        pStatus->appliedPowerLimitPercent = caps.defaultPowerLimitPercent;
        outMsg = "Valores registrados, mas ajuste nao suportado pelo driver/hardware (" + caps.limitationReason + ").";
        return true;
    }

    if (m_hasMock) {
        pStatus->appliedCoreClockOffsetMHz = coreOffsetMHz;
        pStatus->appliedMemoryClockOffsetMHz = memOffsetMHz;
        pStatus->appliedPowerLimitPercent = powerLimitPercent;
        outMsg = "Configuracoes personalizadas de GPU aplicadas com sucesso.";
        return true;
    }

    // Aplicação física no driver de cada fabricante
    std::string hardwareMsg;
    bool ok = false;
    if (caps.vendor == GpuVendor::Nvidia) {
        ok = ApplyNvidia(gpuIndex, coreOffsetMHz, memOffsetMHz, powerLimitPercent, hardwareMsg);
    } else if (caps.vendor == GpuVendor::Amd) {
        ok = ApplyAmd(gpuIndex, coreOffsetMHz, memOffsetMHz, powerLimitPercent, hardwareMsg);
    } else if (caps.vendor == GpuVendor::Intel) {
        ok = ApplyIntel(gpuIndex, coreOffsetMHz, memOffsetMHz, powerLimitPercent, hardwareMsg);
    }

    if (!ok) {
        // Resultado real do driver: os valores "aplicados" permanecem os anteriores.
        outMsg = "Configuracoes de GPU NAO aplicadas pelo driver. " + hardwareMsg;
        return false;
    }

    pStatus->appliedCoreClockOffsetMHz = coreOffsetMHz;
    pStatus->appliedMemoryClockOffsetMHz = memOffsetMHz;
    pStatus->appliedPowerLimitPercent = powerLimitPercent;

    outMsg = "Configuracoes personalizadas de GPU aplicadas. " + hardwareMsg;
    return true;
}

bool GpuControlManager::RestoreDefaults(size_t gpuIndex, std::string& outMsg) {
    return ApplyProfile(gpuIndex, GpuPowerProfile::Balanced, outMsg);
}

void GpuControlManager::SetMockGpu(GpuVendor vendor, const GpuControlCapabilities& caps) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasMock = true;
    m_mockCaps = caps;
    m_mockCaps.vendor = vendor;
    m_mockStatus = GpuControlStatus{};
    m_mockStatus.appliedPowerLimitPercent = caps.defaultPowerLimitPercent;
}

void GpuControlManager::ClearMockGpu() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasMock = false;
}

} // namespace opitcore
#endif // _WIN32 (caminho NVAPI/ADLX/IGCL; o gemelo Linux esta em GpuControlManagerLinux.cpp)
