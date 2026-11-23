// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once

#include "utils/Types.h"
#include <string>
#include <vector>
#include <mutex>
#include <memory>

namespace opitcore {

enum class GpuVendor {
    Nvidia,
    Amd,
    Intel,
    Unknown
};

inline const char* GpuVendorToString(GpuVendor vendor) {
    switch (vendor) {
        case GpuVendor::Nvidia: return "NVIDIA";
        case GpuVendor::Amd: return "AMD";
        case GpuVendor::Intel: return "Intel";
        default: return "Desconhecido";
    }
}

enum class GpuPowerProfile {
    Eco,
    Balanced,
    HighPerformance,
    Custom
};

inline const char* GpuPowerProfileToString(GpuPowerProfile profile) {
    switch (profile) {
        case GpuPowerProfile::Eco: return "Economico";
        case GpuPowerProfile::Balanced: return "Balanceado";
        case GpuPowerProfile::HighPerformance: return "Alto Desempenho";
        case GpuPowerProfile::Custom: return "Personalizado";
        default: return "Desconhecido";
    }
}

inline GpuPowerProfile StringToGpuPowerProfile(const std::string& str) {
    if (str == "Eco" || str == "Economico") return GpuPowerProfile::Eco;
    if (str == "HighPerformance" || str == "Alto Desempenho") return GpuPowerProfile::HighPerformance;
    if (str == "Custom" || str == "Personalizado") return GpuPowerProfile::Custom;
    return GpuPowerProfile::Balanced;
}

struct GpuControlCapabilities {
    bool supported = false;
    GpuVendor vendor = GpuVendor::Unknown;
    std::string adapterName;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;

    bool canControlCoreClock = false;
    int minCoreClockOffsetMHz = -200;
    int maxCoreClockOffsetMHz = 200;
    int defaultCoreClockOffsetMHz = 0;

    bool canControlMemoryClock = false;
    int minMemoryClockOffsetMHz = -200;
    int maxMemoryClockOffsetMHz = 400;
    int defaultMemoryClockOffsetMHz = 0;

    bool canControlPowerLimit = false;
    int minPowerLimitPercent = 60;
    int maxPowerLimitPercent = 110;
    int defaultPowerLimitPercent = 100;

    std::string limitationReason;
};

struct GpuControlStatus {
    GpuPowerProfile currentProfile = GpuPowerProfile::Balanced;

    int requestedCoreClockOffsetMHz = 0;
    int appliedCoreClockOffsetMHz = 0;
    int measuredCoreClockMHz = 0;

    int requestedMemoryClockOffsetMHz = 0;
    int appliedMemoryClockOffsetMHz = 0;
    int measuredMemoryClockMHz = 0;

    int requestedPowerLimitPercent = 100;
    int appliedPowerLimitPercent = 100;
    int measuredPowerWatts = 0;

    double measuredTemperatureC = 0.0;
    int measuredUsagePercent = 0;
};

class GpuControlManager {
public:
    static GpuControlManager& Instance();

    bool Initialize();
    size_t GetGpuCount() const;
    GpuControlCapabilities GetCapabilities(size_t gpuIndex = 0) const;
    GpuControlStatus GetStatus(size_t gpuIndex = 0);

    // Aplicação de perfis e configurações
    bool ApplyProfile(size_t gpuIndex, GpuPowerProfile profile, std::string& outMsg);
    bool ApplyCustomSettings(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg);
    bool RestoreDefaults(size_t gpuIndex, std::string& outMsg);

    // Suporte a mocks controlados para testes determinísticos de fabricantes não disponíveis
    void SetMockGpu(GpuVendor vendor, const GpuControlCapabilities& caps);
    void ClearMockGpu();
    bool HasMockGpu() const { return m_hasMock; }

private:
    GpuControlManager();
    ~GpuControlManager();

    void DetectAdapters();
    void QueryVendorCapabilities(size_t index);

    // Provedores específicos de fabricante
    bool InitNvidiaNvapi();
    bool InitAmdAdlx();
    bool InitIntelIgcl();

    bool ApplyNvidia(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg);
    bool ApplyAmd(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg);
    bool ApplyIntel(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg);

    void UpdateNvidiaTelemetry(size_t gpuIndex);
    void UpdateAmdTelemetry(size_t gpuIndex);
    void UpdateIntelTelemetry(size_t gpuIndex);

    struct GpuHardwareContext {
        void* nativeHandle = nullptr;
        int adlAdapterIndex = -1;
        void* adlxGpu = nullptr;
        bool canPstates = false;
        bool canPower = false;
    };

    mutable std::mutex m_mutex;
    bool m_initialized = false;
    std::vector<GpuControlCapabilities> m_gpus;
    std::vector<GpuControlStatus> m_statuses;
    std::vector<GpuHardwareContext> m_hwContexts;

    // Handles dinâmicos de DLLs de fornecedores
    void* m_nvapiLib = nullptr;
    void* m_adlxLib = nullptr;
    void* m_adlLib = nullptr;
    void* m_igclLib = nullptr;

    bool m_hasMock = false;
    GpuControlCapabilities m_mockCaps;
    GpuControlStatus m_mockStatus;
};

} // namespace opitcore
