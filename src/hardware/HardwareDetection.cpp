// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#include "HardwareDetection.h"
#ifdef _WIN32
#include "platform/windows/GpuDetector.h"
#include "platform/windows/ElevationHelper.h"
#else
#include "platform/linux/GpuDetector.h"
#include "platform/linux/ElevationHelper.h"
#endif
#include "SmbiosProbe.h"
#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>
#endif
#include <array>
#include <cstring>
#include <vector>

namespace opitcore {

#ifdef _WIN32
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
#else
// Gemelo do CPUID: o kernel ja leu o CPUID para /proc/cpuinfo, por isso os mesmos campos
// (vendor_id, model name) dao a mesma descricao sem intrinsic por arquitetura. Em ARM o
// cpuinfo usa chaves diferentes; se nada existir as strings ficam vazias e a UI declara
// o campo como indisponivel em vez de inventar um nome.
static std::string CpuInfoField(const char* key) {
    std::ifstream f("/proc/cpuinfo");
    const std::string prefix = std::string(key) + ":";
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(prefix, 0) != 0) continue;
        const std::string v = line.substr(prefix.size());
        const size_t s = v.find_first_not_of(" \t");
        if (s == std::string::npos) return std::string();
        return v.substr(s);
    }
    return std::string();
}

static std::string CpuVendor() {
    return CpuInfoField("vendor_id");
}

static std::string CpuBrand() {
    std::string brand = CpuInfoField("model name");
    if (brand.empty()) brand = CpuInfoField("Hardware");
    return brand;
}
#endif

CpuDetails HardwareDetection::DetectCpu() {
    CpuDetails ci;
    ci.vendor = CpuVendor();
    ci.brand = CpuBrand();
    ci.origin = DataOrigin::Detected;
    ci.confidence = DataConfidence::Verified;

#ifdef _WIN32
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
#else
    // Threads online: _SC_NPROCESSORS_ONLN e a resposta do kernel ao que o Windows da
    // em dwNumberOfProcessors. Arquitetura vem de uname(2), nao de macros de compilacao,
    // porque o que interessa e o kernel que corre.
    const long online = ::sysconf(_SC_NPROCESSORS_ONLN);
    ci.logicalThreads = (online > 0) ? (int)online : 0;

    struct utsname u{};
    if (::uname(&u) == 0) {
        const std::string m = u.machine;
        if (m == "x86_64") ci.architecture = "x86_64 (AMD64)";
        else if (m == "aarch64") ci.architecture = "ARM64";
        else if (m == "i386" || m == "i486" || m == "i586" || m == "i686") ci.architecture = "x86 (32 bits)";
        else ci.architecture = m;   // nome cru do kernel vale mais que "Desconhecida"
    } else {
        ci.architecture = "Desconhecida";
    }

    // Gemelo do GetLogicalProcessorInformation(RelationProcessorCore): nucleos fisicos
    // sao os pares distintos (physical id, core id) do /proc/cpuinfo. Numa maquina sem
    // esses campos (virtualizado com cpuinfo despido) usa-se "cpu cores" e, na falta
    // dele, o numero de threads -- sempre declarado como Detected, nunca como Verified.
    {
        auto afterColon = [](const std::string& l) {
            const size_t c = l.find(':');
            if (c == std::string::npos) return std::string();
            const size_t s = l.find_first_not_of(" \t", c + 1);
            return (s == std::string::npos) ? std::string() : l.substr(s);
        };
        std::set<std::string> vistos;
        std::ifstream f("/proc/cpuinfo");
        std::string line, physical;
        bool temPhysical = false;
        while (std::getline(f, line)) {
            if (line.rfind("physical id", 0) == 0) {
                physical = afterColon(line);
                temPhysical = true;
            } else if (temPhysical && line.rfind("core id", 0) == 0) {
                vistos.insert(physical + ":" + afterColon(line));
                temPhysical = false;
            }
        }
        int cores = (int)vistos.size();
        if (cores <= 1) {
            const std::string c = CpuInfoField("cpu cores");
            if (!c.empty()) cores = std::atoi(c.c_str());
        }
        ci.physicalCores = (cores > 0) ? cores : ci.logicalThreads;
    }
#endif

    return ci;
}

MemoryDetails HardwareDetection::DetectMemory() {
    MemoryDetails mem;
#ifdef _WIN32
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
#else
    // Os cinco campos do MEMORYSTATUSEX com o que o kernel publica: MemTotal e
    // MemAvailable para a RAM, SwapTotal e SwapFree para o que no Windows e o page
    // file. Sem MemTotal lido nada se preenche: origem e confiancao ficam as do
    // struct (Unknown/Low) em vez de um Medido inventado.
    {
        auto field = [&](const char* key) -> unsigned long long {
            std::ifstream f("/proc/meminfo");
            const std::string prefix = std::string(key) + ":";
            std::string line;
            while (std::getline(f, line)) {
                if (line.rfind(prefix, 0) != 0) continue;
                std::istringstream ss(line.substr(prefix.size()));
                unsigned long long kb = 0;
                if (ss >> kb) return kb;
                return 0;
            }
            return 0;
        };
        const unsigned long long totalKb = field("MemTotal");
        const unsigned long long availKb = field("MemAvailable");
        if (totalKb > 0) {
            mem.totalPhysicalBytes = totalKb * 1024ULL;
            mem.availablePhysicalBytes = availKb * 1024ULL;
            mem.totalPageFileBytes = field("SwapTotal") * 1024ULL;
            mem.availablePageFileBytes = field("SwapFree") * 1024ULL;
            const unsigned long long usedKb = (availKb <= totalKb) ? (totalKb - availKb) : 0;
            mem.memoryLoadPercent = (uint32_t)((usedKb * 100ULL + totalKb / 2ULL) / totalKb);
            mem.origin = DataOrigin::Measured;
            mem.confidence = DataConfidence::Verified;
        }
    }
#endif
    return mem;
}

SystemHardwareInfo HardwareDetection::DetectAll() {
    SystemHardwareInfo info;
    info.cpu = DetectCpu();
    info.memory = DetectMemory();
    info.gpus = GpuDetector::DetectGpus();
    info.osName = ElevationHelper::GetOsVersionString();
#ifdef _WIN32
    info.osVersion = "Windows NT";
#else
    struct utsname lv{};
    info.osVersion = (::uname(&lv) == 0) ? (std::string("Linux ") + lv.release) : std::string("Linux");
#endif
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
