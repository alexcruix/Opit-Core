#include "HardwareDetection.h"
#include "platform/windows/GpuDetector.h"
#include "platform/windows/ElevationHelper.h"
#include "SmbiosProbe.h"
#include <windows.h>
#include <intrin.h>
#include <array>
#include <cstring>
#include <vector>

namespace opitcore {

static std::string CpuVendor() {
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 0);
    std::array<char, 13> buf{};
    *reinterpret_cast<int*>(buf.data()) = info[1];
    *reinterpret_cast<int*>(buf.data() + 4) = info[3];
    *reinterpret_cast<int*>(buf.data() + 8) = info[2];
    return std::string(buf.data());
}

static std::string CpuBrand() {
    int info[4] = {0, 0, 0, 0};
    std::array<char, 49> buf{};
    for (int i = 0; i < 3; i++) {
        __cpuid(info, 0x80000002 + i);
        memcpy(buf.data() + i * 16, info, 16);
    }
    // Remover espaços extras no início
    std::string s(buf.data());
    size_t start = s.find_first_not_of(" ");
    if (start != std::string::npos) {
        return s.substr(start);
    }
    return s;
}

CpuDetails HardwareDetection::DetectCpu() {
    CpuDetails ci;
    ci.vendor = CpuVendor();
    ci.brand = CpuBrand();
    ci.origin = DataOrigin::Detected;
    ci.confidence = DataConfidence::Verified;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    ci.logicalThreads = (int)si.dwNumberOfProcessors;

    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        ci.architecture = "x64 (AMD64)";
    } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
        ci.architecture = "ARM64";
    } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        ci.architecture = "x86";
    } else {
        ci.architecture = "Desconhecida";
    }

    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    if (len > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> v(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(v.data(), &len)) {
            int cores = 0;
            for (auto& e : v) {
                if (e.Relationship == RelationProcessorCore) {
                    cores++;
                }
            }
            ci.physicalCores = (cores > 0) ? cores : ci.logicalThreads;
        }
    } else {
        ci.physicalCores = ci.logicalThreads;
    }

    return ci;
}

MemoryDetails HardwareDetection::DetectMemory() {
    MemoryDetails mem;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        mem.totalPhysicalBytes = ms.ullTotalPhys;
        mem.availablePhysicalBytes = ms.ullAvailPhys;
        mem.totalPageFileBytes = ms.ullTotalPageFile;
        mem.availablePageFileBytes = ms.ullAvailPageFile;
        mem.memoryLoadPercent = ms.dwMemoryLoad;
        mem.origin = DataOrigin::Measured;
        mem.confidence = DataConfidence::Verified;
    }
    return mem;
}

SystemHardwareInfo HardwareDetection::DetectAll() {
    SystemHardwareInfo info;
    info.cpu = DetectCpu();
    info.memory = DetectMemory();
    info.gpus = GpuDetector::DetectGpus();
    info.osName = ElevationHelper::GetOsVersionString();
    info.osVersion = "Windows NT";
    info.is64Bit = ElevationHelper::Is64BitOs();

    const SmbiosSnapshot smb = SmbiosProbe::Probe();
    if (smb.ok) {
        info.bios = smb.bios;
        info.motherboard = smb.board;
        info.memorySticks = smb.sticks;
    }
    return info;
}

} // namespace opitcore
