// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#include "GpuDetector.h"
#include "core/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <vector>

namespace opitcore {
namespace {

// So os campos e assinaturas que aqui se usam, tal como no Windows: nada de cabecalhos
// de driver no build. Os codigos de retorno sao inteiros do ABI publico do NVML (0 = OK).
typedef void* NvmlDevice;
struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};
typedef int (*NvmlInitFn)(void);
typedef int (*NvmlShutdownFn)(void);
typedef int (*NvmlDeviceGetCountFn)(unsigned int*);
typedef int (*NvmlGetHandleFn)(unsigned int, NvmlDevice*);
typedef int (*NvmlGetNameFn)(NvmlDevice, char*, unsigned int);
typedef int (*NvmlGetMemoryFn)(NvmlDevice, NvmlMemory*);

struct NvmlApi {
    void* handle = nullptr;
    NvmlInitFn init = nullptr;
    NvmlShutdownFn shutdown = nullptr;
    NvmlDeviceGetCountFn getCount = nullptr;
    NvmlGetHandleFn getHandle = nullptr;
    NvmlGetNameFn getName = nullptr;
    NvmlGetMemoryFn getMemory = nullptr;
    bool usable = false;
};

NvmlApi LoadNvml() {
    NvmlApi api;
    static const char* candidates[] = {
        "libnvidia-ml.so.1",
        "/usr/lib/wsl/lib/libnvidia-ml.so.1"   // no WSL2 o NVML so existe neste caminho
    };
    for (const char* path : candidates) {
        api.handle = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (api.handle) break;
    }
    if (!api.handle) return api;
    api.init = reinterpret_cast<NvmlInitFn>(::dlsym(api.handle, "nvmlInit_v2"));
    api.shutdown = reinterpret_cast<NvmlShutdownFn>(::dlsym(api.handle, "nvmlShutdown"));
    api.getCount = reinterpret_cast<NvmlDeviceGetCountFn>(::dlsym(api.handle, "nvmlDeviceGetCount_v2"));
    api.getHandle = reinterpret_cast<NvmlGetHandleFn>(::dlsym(api.handle, "nvmlDeviceGetHandleByIndex_v2"));
    api.getName = reinterpret_cast<NvmlGetNameFn>(::dlsym(api.handle, "nvmlDeviceGetName"));
    api.getMemory = reinterpret_cast<NvmlGetMemoryFn>(::dlsym(api.handle, "nvmlDeviceGetMemoryInfo"));
    api.usable = api.init && api.getCount && api.getHandle && api.getName && api.getMemory;
    if (!api.usable) {
        OPIT_LOG_WARN("GpuDetector(Linux): NVML carregado mas sem as funcoes de enumeracao; GPU NVIDIA indisponivel.");
    }
    return api;
}

std::string ReadLine(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

unsigned long ReadHex(const std::string& path, bool& ok) {
    ok = false;
    const std::string s = ReadLine(path);
    if (s.empty()) return 0;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s.c_str(), &end, 16);
    if (end == s.c_str()) return 0;
    ok = true;
    return v;
}

unsigned long long ReadDecimal(const std::string& path, bool& ok) {
    ok = false;
    const std::string s = ReadLine(path);
    if (s.empty()) return 0;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;
    ok = true;
    return v;
}

std::string PciLabel(const std::string& devDir) {
    std::ifstream ue(devDir + "/uevent");
    std::string line;
    while (std::getline(ue, line)) {
        if (line.rfind("PCI_LABEL=", 0) == 0) return line.substr(10);
    }
    return std::string();
}

// Os controladores que nao passam pelo NVML (amdgpu, i915, nouveau) publicam-se em
// /sys/class/drm/cardN. Um card sem os ficheiros vendor/device do PCI -- e o caso do
// vgem do WSL2, um esboco virtual -- nao e uma GPU a reportar: sem descricao nem
// memoria inventadas. skipNvidia evita contar duas vezes a placa que o NVML ja deu.
void CollectDrmGpus(bool skipNvidia, std::vector<GpuDetails>& out) {
    // Mesma raiz configuravel do gemelo de controle: sem fixture nao ha como exercitar o
    // caminho amdgpu/i915 numa maquina que so tem NVIDIA ou um vgem virtual.
    const char* rootEnv = std::getenv("OPITCORE_DRM_ROOT");
    const std::string drmRoot = (rootEnv && *rootEnv) ? std::string(rootEnv) : std::string("/sys/class/drm");
    DIR* dir = ::opendir(drmRoot.c_str());
    if (!dir) return;
    while (dirent* e = ::readdir(dir)) {
        if (std::strncmp(e->d_name, "card", 4) != 0) continue;
        const char* tail = e->d_name + 4;
        if (*tail < '0' || *tail > '9') continue;
        if (std::strchr(tail, '-')) continue;   // conectores (card0-HDMI-A-1) nao sao dispositivos
        const std::string dev = drmRoot + "/" + e->d_name + "/device";
        bool okV = false;
        bool okD = false;
        const unsigned long vendor = ReadHex(dev + "/vendor", okV);
        const unsigned long device = ReadHex(dev + "/device", okD);
        if (!okV || !okD) continue;
        if (skipNvidia && vendor == 0x10DE) continue;

        char id[16];
        std::snprintf(id, sizeof(id), "%04lx:%04lx", vendor, device);
        GpuDetails g;
        g.vendorId = static_cast<uint32_t>(vendor);
        g.deviceId = static_cast<uint32_t>(device);
        std::string label = ReadLine(dev + "/label");
        if (label.empty()) label = PciLabel(dev);
        g.description = label.empty() ? (std::string("VGA ") + id) : label;
        bool okMem = false;
        const unsigned long long vram = ReadDecimal(dev + "/mem_info_vram_total", okMem);
        g.dedicatedVideoMemoryMB = okMem ? static_cast<uint64_t>(vram / (1024ULL * 1024ULL)) : 0;
        g.isDedicated = (vendor != 0x8086);
        g.origin = DataOrigin::Detected;
        g.confidence = label.empty() ? DataConfidence::Medium : DataConfidence::High;
        out.push_back(g);
    }
    ::closedir(dir);
}

} // namespace

std::vector<GpuDetails> GpuDetector::DetectGpus() {
    std::vector<GpuDetails> out;
    const NvmlApi api = LoadNvml();
    bool nvmlGaveGpus = false;

    if (api.usable && api.init() == 0) {
        unsigned int count = 0;
        if (api.getCount(&count) == 0) {
            for (unsigned int i = 0; i < count; ++i) {
                NvmlDevice dev = nullptr;
                if (api.getHandle(i, &dev) != 0 || !dev) continue;
                GpuDetails g;
                char name[96] = {0};
                if (api.getName(dev, name, sizeof(name) - 1) == 0) {
                    g.description = name;
                }
                NvmlMemory mem{};
                if (api.getMemory(dev, &mem) == 0) {
                    g.dedicatedVideoMemoryMB = static_cast<uint64_t>(mem.total >> 20);
                }
                // 0x10de nao e chute: o NVML so enumera dispositivos NVIDIA. O deviceId
                // especifico nao e pedido ao NVML neste build, por isso fica 0 e a busca
                // de specs usa a descricao, como no caminho Windows quando falta o ID.
                g.vendorId = 0x10DE;
                g.deviceId = 0;
                g.isDedicated = true;
                g.origin = DataOrigin::Detected;
                g.confidence = g.description.empty() ? DataConfidence::Low : DataConfidence::High;
                out.push_back(g);
                nvmlGaveGpus = true;
            }
        }
        if (api.shutdown) api.shutdown();
    }
    if (api.handle) ::dlclose(api.handle);

    CollectDrmGpus(nvmlGaveGpus, out);

    if (out.empty()) {
        OPIT_LOG_WARN("GpuDetector(Linux): nenhuma GPU enumerada (NVML ausente e sem card PCI legivel em /sys/class/drm).");
    }
    return out;
}

} // namespace opitcore
