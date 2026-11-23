// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace opitcore {

// Classificação de confiabilidade e origem de dados conforme requisitos da arquitetura
enum class DataOrigin { Factory, Detected, Measured, Estimated, Unknown };
enum class DataConfidence { Low, Medium, High, Verified };

inline const char* DataOriginToString(DataOrigin origin) {
    switch (origin) {
        case DataOrigin::Factory: return "Factory";
        case DataOrigin::Detected: return "Detected";
        case DataOrigin::Measured: return "Measured";
        case DataOrigin::Estimated: return "Estimated";
        default: return "Unknown";
    }
}

inline const char* DataConfidenceToString(DataConfidence conf) {
    switch (conf) {
        case DataConfidence::Verified: return "Verified";
        case DataConfidence::High: return "High";
        case DataConfidence::Medium: return "Medium";
        default: return "Low";
    }
}

struct CpuDetails {
    DataOrigin origin = DataOrigin::Detected;
    DataConfidence confidence = DataConfidence::Verified;
    std::string vendor;
    std::string brand;
    int physicalCores = 0;
    int logicalThreads = 0;
    std::string architecture;
};

struct MemoryDetails {
    DataOrigin origin = DataOrigin::Measured;
    DataConfidence confidence = DataConfidence::Verified;
    uint64_t totalPhysicalBytes = 0;
    uint64_t availablePhysicalBytes = 0;
    uint64_t totalPageFileBytes = 0;
    uint64_t availablePageFileBytes = 0;
    uint32_t memoryLoadPercent = 0;
};

struct GpuDetails {
    DataOrigin origin = DataOrigin::Detected;
    DataConfidence confidence = DataConfidence::High;
    std::string description;
    uint64_t dedicatedVideoMemoryMB = 0;
    uint64_t sharedSystemMemoryMB = 0;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    bool isDedicated = false;
};

struct DocumentedHardwareSpec {
    bool found = false;
    std::string category;
    std::string family;
    std::string model;
    std::string coresThreads;
    std::string clockBase;
    std::string clockBoost;
    std::string tdp;
    std::string socketOrBus;
    std::string details;
};

struct BiosInfo {
    DataOrigin origin = DataOrigin::Unknown;
    DataConfidence confidence = DataConfidence::Low;
    std::string vendor;
    std::string version;
    std::string releaseDate;
};

struct MotherboardDetails {
    DataOrigin origin = DataOrigin::Unknown;
    DataConfidence confidence = DataConfidence::Low;
    std::string manufacturer;
    std::string product;
    std::string version;
    std::string chassisType;
};

struct MemoryStickInfo {
    std::string locator;            // Slot conforme firmware (ex: "ChannelA-DIMM0")
    std::string manufacturer;
    std::string partNumber;
    std::string formFactor;         // DIMM / SODIMM
    std::string memoryType;         // DDR4 / DDR5 / LPDDR5
    uint32_t capacityMB = 0;
    uint32_t speedMHz = 0;          // Velocidade nominal do pente
    uint32_t configuredSpeedMHz = 0;// Velocidade efetiva (SMBIOS 2.6+)
};

struct SystemHardwareInfo {
    CpuDetails cpu;
    MemoryDetails memory;
    std::vector<GpuDetails> gpus;
    BiosInfo bios;
    MotherboardDetails motherboard;
    std::vector<MemoryStickInfo> memorySticks;
    std::string osName;
    std::string osVersion;
    bool is64Bit = true;
    DocumentedHardwareSpec documentedCpu;
    DocumentedHardwareSpec documentedGpu;
};

struct ProcessItem {
    uint32_t pid = 0;
    std::string name;
    uint64_t workingSetBytes = 0; // Working set memory
    uint32_t priorityClass = 0;
    std::string priorityName;
    uint64_t affinityMask = 0;
    bool isSystemCritical = false;
    // PPL/PP: o kernel recusa escrita de prioridade e nao ha elevacao nem certificado que abra. Medido
    // via NtQueryInformationProcess(ProcessProtectionInformation) no mesmo handle da enumeracao.
    bool protectedByWindows = false;
    std::string imagePath;      // QueryFullProcessImageNameW; vazio quando o processo e protegido
    uint64_t cpuTime100ns = 0;  // kernel + user acumulado (base da medicao de fome/ociosidade)
};

struct SystemMetricsSnapshot {
    double cpuUsagePercent = 0.0;
    double ramUsagePercent = 0.0;
    uint64_t ramUsedMB = 0;
    uint64_t ramTotalMB = 0;
    double cpuTemperatureC = 0.0;
    bool hasTempSensor = false;
    std::string tempSensorSource;
    uint32_t fanRpm = 0;
    bool hasFanRpm = false;
    std::string activeProfileName;
    std::string activePowerSchemeName;
    std::string activePowerSchemeGuid;
    bool isThermalThrottling = false;
    bool isGameRunning = false;
    std::string runningGameName;
    // Telemetria real para funcao-objetivo: tempo por quadro do compositor (DWM).
    // 0.0/false = indisponivel (sem DWM ou primeira amostra) - nunca inventar valor.
    double frameTimeMs = 0.0;
    bool hasFrameTime = false;
    // Potencia da GPU medida pelo driver (NVAPI/ADL/IGCL), onde existir sensor.
    double gpuPowerWatts = 0.0;
    bool hasGpuPowerW = false;
};

} // namespace opitcore
