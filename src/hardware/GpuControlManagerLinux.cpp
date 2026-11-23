// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#ifndef _WIN32

// Gemelo Linux do caminho NVAPI/ADLX/IGCL. Mesma superficie publica e o mesmo vocabulario de
// mensagens (a UI compara texto), mas cada prova de capacidade vem do que o kernel e o driver
// NVIDIA expoe aqui: NVML para NVIDIA (mesmo desenho de import dinamico do Windows, para nao
// pedir cabecalho de driver no build) e sysfs para amdgpu e i915/xe. Onde o Linux nao tem
// mecanismo, a capacidade fica false com o motivo real em vez de um sucesso inventado.
//
// Restauro = estado EXATO: os unicos "padrao" que este ficheiro escreve sao (a) os valores
// originais capturados por leitura antes de qualquer escrita no Intel e (b) os resets que o
// proprio NVML define como devolver ao padrao do driver.

#include "GpuControlManager.h"
#include "platform/linux/GpuDetector.h"
#include "core/Logger.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

namespace opitcore {
namespace {

// ---------------------------------------------------------------------------
// sysfs: ler, escrever e reler. Uma escrita em sysfs so se considera aplicada
// depois de o kernel a devolver -- e isto vale tambem para 32 bits, onde os
// ficheiros sao os mesmos.
// ---------------------------------------------------------------------------
bool ReadU64File(const std::string& path, unsigned long long& out) {
    std::ifstream f(path);
    std::string token;
    if (!(f >> token)) return false;
    const char* begin = token.c_str();
    char* end = nullptr;
    const unsigned long long v = std::strtoull(begin, &end, 10);
    if (end == begin) return false;
    out = v;
    return true;
}

bool WriteU64File(const std::string& path, unsigned long long value) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << value;
    f.flush();
    if (!f) return false;
    unsigned long long back = 0;
    return ReadU64File(path, back) && back == value;
}

bool FileExists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

bool IsDir(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

std::vector<std::string> ListDirNames(const std::string& dir) {
    std::vector<std::string> names;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return names;
    while (struct dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

bool ReadHexFile(const std::string& path, unsigned long& out) {
    std::ifstream f(path);
    std::string token;
    if (!(f >> token)) return false;
    char* end = nullptr;
    out = std::strtoul(token.c_str(), &end, 16);
    return end != token.c_str();
}

// hwmon de um adaptador DRM: /sys/class/drm/cardN/device/hwmon/hwmonM.
std::string FindHwmonDir(const std::string& deviceDir) {
    const std::string base = deviceDir + "/hwmon";
    for (const std::string& m : ListDirNames(base)) {
        if (m.rfind("hwmon", 0) == 0 && IsDir(base + "/" + m)) return base + "/" + m;
    }
    return std::string();
}

// Os ficheiros de frequencia do i915 moram no proprio card dir; no driver xe moram
// dois niveis abaixo de device/ (tile0/gtgN). Procura-se o nome do ficheiro, nao se
// assume o caminho, porque as duas geracoes de driver coexistem no mesmo Ubuntu.
bool FindGtDir(const std::string& root, const std::string& probeFile, std::string& outDir, int depth) {
    if (depth < 0) return false;
    if (FileExists(root + "/" + probeFile)) { outDir = root; return true; }
    for (const std::string& n : ListDirNames(root)) {
        if (n == "." || n == "..") continue;
        const std::string child = root + "/" + n;
        if (!IsDir(child)) continue;
        if (FindGtDir(child, probeFile, outDir, depth - 1)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// NVML: ABI declarada a mao, carregada por dlopen.
// ---------------------------------------------------------------------------
constexpr int kNvmlSuccess = 0;
constexpr int kNvmlClockGraphics = 0;
constexpr int kNvmlClockMemory = 2;
constexpr int kNvmlTempGpu = 0;
constexpr size_t kMaxSupportedClocks = 64;

struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};

struct NvmlRuntime {
    void* lib = nullptr;
    bool ready = false;

    int (*init)() = nullptr;
    int (*deviceGetCount)(unsigned int*) = nullptr;
    int (*deviceGetHandleByIndex)(unsigned int, void**) = nullptr;
    int (*deviceGetName)(void*, char*, unsigned int) = nullptr;
    int (*deviceGetClockInfo)(void*, unsigned int, unsigned int*) = nullptr;
    int (*deviceGetMaxClockInfo)(void*, unsigned int, unsigned int*) = nullptr;
    int (*deviceGetSupportedMemoryClocks)(void*, unsigned int*, unsigned int*) = nullptr;
    int (*deviceGetSupportedGraphicsClocks)(void*, unsigned int, unsigned int*, unsigned int*) = nullptr;
    int (*deviceSetGpuLockedClocks)(void*, unsigned int, unsigned int) = nullptr;
    int (*deviceResetGpuLockedClocks)(void*) = nullptr;
    int (*deviceSetMemoryLockedClocks)(void*, unsigned int, unsigned int) = nullptr;
    int (*deviceResetMemoryLockedClocks)(void*) = nullptr;
    int (*deviceGetTemperature)(void*, unsigned int, unsigned int*) = nullptr;
    int (*deviceGetUtilizationRates)(void*, NvmlUtilization*) = nullptr;
    int (*deviceGetPowerUsage)(void*, unsigned int*) = nullptr;
    int (*deviceGetEnforcedPowerLimit)(void*, unsigned int*) = nullptr;
    int (*deviceGetPowerManagementLimitConstraints)(void*, unsigned int*, unsigned int*) = nullptr;
    int (*deviceSetPowerManagementLimit)(void*, unsigned int) = nullptr;
};

NvmlRuntime& Nvml() {
    static NvmlRuntime r;
    return r;
}

bool BindNvmlSymbol(NvmlRuntime& r, const char* symbol, void** slot) {
    void* p = dlsym(r.lib, symbol);
    if (!p) return false;
    *slot = p;
    return true;
}

// Carrega e inicializa o NVML uma vez por processo. Os candidatos sao os mesmos
// que o detetor de GPU usa, para que a detecao e o controle vejam o mesmo driver.
bool LoadNvml() {
    NvmlRuntime& r = Nvml();
    if (r.ready) return true;
    if (r.lib) return false;

    const char* candidates[] = {
        "libnvidia-ml.so.1",
        "/usr/lib/wsl/lib/libnvidia-ml.so.1",
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1",
        "/usr/lib/i386-linux-gnu/libnvidia-ml.so.1"
    };
    for (const char* name : candidates) {
        r.lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (r.lib) break;
    }
    if (!r.lib) return false;

    struct Binding { const char* symbol; void** slot; };
    Binding bindings[] = {
        { "nvmlInit_v2", reinterpret_cast<void**>(&r.init) },
        { "nvmlDeviceGetCount_v2", reinterpret_cast<void**>(&r.deviceGetCount) },
        { "nvmlDeviceGetHandleByIndex_v2", reinterpret_cast<void**>(&r.deviceGetHandleByIndex) },
        { "nvmlDeviceGetName", reinterpret_cast<void**>(&r.deviceGetName) },
        { "nvmlDeviceGetClockInfo", reinterpret_cast<void**>(&r.deviceGetClockInfo) },
        { "nvmlDeviceGetMaxClockInfo", reinterpret_cast<void**>(&r.deviceGetMaxClockInfo) },
        { "nvmlDeviceGetSupportedMemoryClocks", reinterpret_cast<void**>(&r.deviceGetSupportedMemoryClocks) },
        { "nvmlDeviceGetSupportedGraphicsClocks", reinterpret_cast<void**>(&r.deviceGetSupportedGraphicsClocks) },
        { "nvmlDeviceSetGpuLockedClocks", reinterpret_cast<void**>(&r.deviceSetGpuLockedClocks) },
        { "nvmlDeviceResetGpuLockedClocks", reinterpret_cast<void**>(&r.deviceResetGpuLockedClocks) },
        { "nvmlDeviceSetMemoryLockedClocks", reinterpret_cast<void**>(&r.deviceSetMemoryLockedClocks) },
        { "nvmlDeviceResetMemoryLockedClocks", reinterpret_cast<void**>(&r.deviceResetMemoryLockedClocks) },
        { "nvmlDeviceGetTemperature", reinterpret_cast<void**>(&r.deviceGetTemperature) },
        { "nvmlDeviceGetUtilizationRates", reinterpret_cast<void**>(&r.deviceGetUtilizationRates) },
        { "nvmlDeviceGetPowerUsage", reinterpret_cast<void**>(&r.deviceGetPowerUsage) },
        { "nvmlDeviceGetEnforcedPowerLimit", reinterpret_cast<void**>(&r.deviceGetEnforcedPowerLimit) },
        { "nvmlDeviceGetPowerManagementLimitConstraints", reinterpret_cast<void**>(&r.deviceGetPowerManagementLimitConstraints) },
        { "nvmlDeviceSetPowerManagementLimit", reinterpret_cast<void**>(&r.deviceSetPowerManagementLimit) },
    };
    for (Binding& b : bindings) {
        BindNvmlSymbol(r, b.symbol, b.slot);
    }

    if (!r.init || !r.deviceGetCount || !r.deviceGetHandleByIndex) {
        dlclose(r.lib);
        r.lib = nullptr;
        return false;
    }
    if (r.init() != kNvmlSuccess) {
        dlclose(r.lib);
        r.lib = nullptr;
        return false;
    }
    r.ready = true;
    return true;
}

// ---------------------------------------------------------------------------
// Contexto por GPU para os caminhos de sysfs, em passo com m_gpus.
// ---------------------------------------------------------------------------
struct SysfsGpuPaths {
    std::string cardDir;
    std::string deviceDir;
    std::string hwmonDir;
    std::string gtDir;
    unsigned long long origGtMinMHz = 0;
    unsigned long long origGtMaxMHz = 0;
    bool hasOrigGt = false;
    std::string powerCapFile;      // power1_cap | power1_max (microWatts)
    unsigned long long defaultPowerMicrowatts = 0;
    std::string coreFreqFile;      // freq1_input (amdgpu) | cur_freq (devfreq)
};

std::vector<SysfsGpuPaths>& GpuPaths() {
    static std::vector<SysfsGpuPaths> v;
    return v;
}

// Todas as placas DRM com IDs PCI legiveis, por ordem de nome (card0, card1, ...),
// que e a mesma ordem que o detetor de GPU usa ao publicar a lista.
struct DrmCard {
    std::string cardDir;
    std::string deviceDir;
    unsigned long vendorId = 0;
    unsigned long deviceId = 0;
};

std::vector<DrmCard> CollectDrmCards() {
    std::vector<DrmCard> cards;
    const char* rootEnv = std::getenv("OPITCORE_DRM_ROOT");
    const std::string drmRoot = (rootEnv && *rootEnv) ? std::string(rootEnv) : std::string("/sys/class/drm");
    for (const std::string& name : ListDirNames(drmRoot)) {
        if (name.rfind("card", 0) != 0) continue;
        if (name.find('-') != std::string::npos) continue;   // card1-HDMI-A-1 e conector, nao adaptador
        const std::string cardDir = drmRoot + "/" + name;
        const std::string deviceDir = cardDir + "/device";
        unsigned long vendor = 0, device = 0;
        if (!ReadHexFile(deviceDir + "/vendor", vendor)) continue;
        if (!ReadHexFile(deviceDir + "/device", device)) continue;
        DrmCard c;
        c.cardDir = cardDir;
        c.deviceDir = deviceDir;
        c.vendorId = vendor;
        c.deviceId = device;
        cards.push_back(c);
    }
    return cards;
}

// Liga cada entrada publicada pelo detetor de GPU ao seu cartao DRM, seja qual for o
// fabricante. Vale tambem para NVIDIA: sob WSL2 o NVML enumera e da telemetria, mas o
// hwmon do driver DRM e o que realmente tem temperatura e consumo.
void BindDrmCards(std::vector<GpuControlCapabilities>& gpus, std::vector<SysfsGpuPaths>& paths) {
    const std::vector<DrmCard> cards = CollectDrmCards();
    std::vector<bool> usada(cards.size(), false);
    for (size_t i = 0; i < gpus.size() && i < paths.size(); ++i) {
        if (!gpus[i].vendorId) continue;
        for (size_t c = 0; c < cards.size(); ++c) {
            if (usada[c]) continue;
            if (cards[c].vendorId != gpus[i].vendorId) continue;
            // O deviceId so discrimina quando o detetor o le (o NVML nao o publica).
            if (gpus[i].deviceId && cards[c].deviceId && cards[c].deviceId != gpus[i].deviceId) continue;
            paths[i].cardDir = cards[c].cardDir;
            paths[i].deviceDir = cards[c].deviceDir;
            paths[i].hwmonDir = FindHwmonDir(cards[c].deviceDir);
            usada[c] = true;
            break;
        }
    }
}

// Percentagem -> valor absoluto, arredondado para o inteiro mais proximo, na
// convensao do Windows (100% = limite imposto pelo driver/firmware).
unsigned long long PercentToValue(unsigned long long baseValue, int percent) {
    if (baseValue == 0) return 0;
    return (baseValue * static_cast<unsigned long long>(percent) + 50ULL) / 100ULL;
}

int ValueToPercent(unsigned long long value, unsigned long long baseValue) {
    if (baseValue == 0) return 0;
    return static_cast<int>((value * 100ULL + baseValue / 2ULL) / baseValue);
}

// Escolhe o relogio suportado mais proximo do pedido: o NVML so aceita valores da
// tabela do VBIOS, e dizer "750 MHz" quando a tabela tem 748/759 seria mentir no
// campo aplicado.
bool PickSupported(const std::vector<unsigned int>& supported, unsigned int target, unsigned int& out) {
    if (supported.empty()) return false;
    unsigned int best = supported[0];
    for (unsigned int c : supported) {
        const long dc = std::labs(static_cast<long>(c) - static_cast<long>(target));
        const long db = std::labs(static_cast<long>(best) - static_cast<long>(target));
        if (dc < db) best = c;
    }
    out = best;
    return true;
}

bool ReadSupportedClocks(void* device, bool graphics, unsigned int memClockHz, std::vector<unsigned int>& out) {
    NvmlRuntime& r = Nvml();
    unsigned int buf[kMaxSupportedClocks] = {0};
    unsigned int count = kMaxSupportedClocks;
    int rc;
    if (graphics) {
        if (!r.deviceGetSupportedGraphicsClocks) return false;
        rc = r.deviceGetSupportedGraphicsClocks(device, memClockHz, &count, buf);
    } else {
        if (!r.deviceGetSupportedMemoryClocks) return false;
        rc = r.deviceGetSupportedMemoryClocks(device, &count, buf);
    }
    if (rc != kNvmlSuccess || count == 0 || count > kMaxSupportedClocks) return false;
    out.assign(buf, buf + count);
    return true;
}

} // namespace

// ============================================================================
// Singleton, construtores e a superficie independente de plataforma
// ============================================================================
GpuControlManager& GpuControlManager::Instance() {
    static GpuControlManager instance;
    return instance;
}

GpuControlManager::GpuControlManager() = default;

GpuControlManager::~GpuControlManager() {
    // O NVML nao se descarrega: a propria NVIDIA desaconselha o unload do driver
    // library em vida do processo, e o fecho do processo ja devolve a mapeacao.
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
        const auto vendor = m_gpus[gpuIndex].vendor;
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

// ============================================================================
// Detecao e ligacao dos provedores
// ============================================================================
void GpuControlManager::DetectAdapters() {
    m_gpus.clear();
    m_statuses.clear();
    m_hwContexts.clear();
    GpuPaths().clear();

    const auto detected = GpuDetector::DetectGpus();
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
    GpuPaths().resize(m_gpus.size());

    BindDrmCards(m_gpus, GpuPaths());
    InitNvidiaNvapi();
    InitAmdAdlx();
    InitIntelIgcl();

    for (size_t i = 0; i < m_gpus.size(); ++i) {
        QueryVendorCapabilities(i);
    }

    if (m_gpus.empty()) {
        OPIT_LOG_INFO("[GPU] Nenhum adaptador de video detectado via NVML ou sysfs DRM.");
    }
}

bool GpuControlManager::InitNvidiaNvapi() {
    NvmlRuntime& r = Nvml();
    if (!LoadNvml()) return false;

    unsigned int count = 0;
    if (!r.deviceGetCount || r.deviceGetCount(&count) != kNvmlSuccess) return false;

    int nvIdx = 0;
    for (size_t i = 0; i < m_gpus.size() && static_cast<unsigned int>(nvIdx) < count; ++i) {
        if (m_gpus[i].vendor != GpuVendor::Nvidia) continue;
        void* device = nullptr;
        if (r.deviceGetHandleByIndex(static_cast<unsigned int>(nvIdx), &device) != kNvmlSuccess || !device) {
            ++nvIdx;
            continue;
        }
        m_hwContexts[i].nativeHandle = device;
        ++nvIdx;
    }

    OPIT_LOG_INFO(std::string("[GPU] Provedor NVIDIA NVML carregado com ") +
                  std::to_string(count) + " dispositivo(s) na lista do driver.");
    return true;
}

bool GpuControlManager::InitAmdAdlx() {
    size_t bound = 0;
    for (size_t i = 0; i < m_gpus.size() && i < GpuPaths().size(); ++i) {
        if (m_gpus[i].vendor != GpuVendor::Amd) continue;
        SysfsGpuPaths& p = GpuPaths()[i];
        if (p.cardDir.empty()) continue;
        if (p.hwmonDir.empty()) p.hwmonDir = FindHwmonDir(p.cardDir);

        // O amdgpu publica o limite aplicavel como power1_cap em kernels novos e como
        // power1_max nos antigos; procura-se nos dois niveis (device/ e hwmon/).
        const std::string dirs[] = { p.deviceDir, p.hwmonDir };
        for (const std::string& dir : dirs) {
            if (dir.empty() || !p.powerCapFile.empty()) continue;
            if (FileExists(dir + "/power1_cap")) p.powerCapFile = dir + "/power1_cap";
            else if (FileExists(dir + "/power1_max")) p.powerCapFile = dir + "/power1_max";
        }
        if (!p.powerCapFile.empty()) {
            unsigned long long cap = 0;
            if (ReadU64File(p.powerCapFile, cap)) p.defaultPowerMicrowatts = cap;
        }
        if (FileExists(p.deviceDir + "/freq1_input")) p.coreFreqFile = p.deviceDir + "/freq1_input";
        else if (!p.hwmonDir.empty() && FileExists(p.hwmonDir + "/freq1_input")) p.coreFreqFile = p.hwmonDir + "/freq1_input";
        m_hwContexts[i].adlAdapterIndex = static_cast<int>(i);
        ++bound;
    }
    if (bound == 0) return false;
    OPIT_LOG_INFO("[GPU] Adaptadores AMD ligados pelo sysfs amdgpu (" + std::to_string(bound) + ").");
    return true;
}

bool GpuControlManager::InitIntelIgcl() {
    size_t bound = 0;
    for (size_t i = 0; i < m_gpus.size() && i < GpuPaths().size(); ++i) {
        if (m_gpus[i].vendor != GpuVendor::Intel) continue;
        SysfsGpuPaths& p = GpuPaths()[i];
        if (p.cardDir.empty()) continue;

        if (!FindGtDir(p.cardDir, "gt_cur_freq_mhz", p.gtDir, 3) &&
            !FindGtDir(p.deviceDir, "gt_min_freq_mhz", p.gtDir, 3)) {
            p.gtDir.clear();
        }
        if (!p.gtDir.empty()) {
            p.hasOrigGt = ReadU64File(p.gtDir + "/gt_min_freq_mhz", p.origGtMinMHz) &&
                          ReadU64File(p.gtDir + "/gt_max_freq_mhz", p.origGtMaxMHz);
        }
        const std::string dirs[] = { p.hwmonDir, p.deviceDir };
        for (const std::string& dir : dirs) {
            if (dir.empty() || !p.powerCapFile.empty()) continue;
            if (FileExists(dir + "/power1_cap")) p.powerCapFile = dir + "/power1_cap";
            else if (FileExists(dir + "/power1_max")) p.powerCapFile = dir + "/power1_max";
        }
        if (!p.powerCapFile.empty()) {
            unsigned long long cap = 0;
            if (ReadU64File(p.powerCapFile, cap)) p.defaultPowerMicrowatts = cap;
        }
        if (!p.gtDir.empty() && FileExists(p.gtDir + "/gt_cur_freq_mhz")) {
            p.coreFreqFile = p.gtDir + "/gt_cur_freq_mhz";
        }
        m_hwContexts[i].adlAdapterIndex = static_cast<int>(i);
        ++bound;
    }
    if (bound == 0) return false;
    OPIT_LOG_INFO("[GPU] Adaptadores Intel ligados pelo sysfs i915/xe (" + std::to_string(bound) + ").");
    return true;
}

// ============================================================================
// Provas de capacidade, por fabricante
// ============================================================================
void GpuControlManager::QueryVendorCapabilities(size_t index) {
    if (index >= m_gpus.size()) return;
    auto& caps = m_gpus[index];
    auto& ctx = m_hwContexts[index];

    switch (caps.vendor) {
    case GpuVendor::Nvidia: {
        NvmlRuntime& r = Nvml();
        if (r.ready && ctx.nativeHandle) {
            void* d = ctx.nativeHandle;

            unsigned int maxGfx = 0;
            const bool haveMaxGfx = r.deviceGetMaxClockInfo &&
                r.deviceGetMaxClockInfo(d, kNvmlClockGraphics, &maxGfx) == kNvmlSuccess && maxGfx > 0;
            unsigned int maxMem = 0;
            const bool haveMaxMem = r.deviceGetMaxClockInfo &&
                r.deviceGetMaxClockInfo(d, kNvmlClockMemory, &maxMem) == kNvmlSuccess && maxMem > 0;

            std::vector<unsigned int> gfxList;
            std::vector<unsigned int> memList;
            if (haveMaxGfx) ReadSupportedClocks(d, true, maxMem ? maxMem : 0, gfxList);
            if (haveMaxMem) ReadSupportedClocks(d, false, 0, memList);

            caps.canControlCoreClock = haveMaxGfx && !gfxList.empty() && r.deviceSetGpuLockedClocks &&
                                       r.deviceResetGpuLockedClocks;
            caps.canControlMemoryClock = haveMaxMem && !memList.empty() && r.deviceSetMemoryLockedClocks &&
                                         r.deviceResetMemoryLockedClocks;
            caps.minCoreClockOffsetMHz = -200;
            caps.maxCoreClockOffsetMHz = 200;
            caps.defaultCoreClockOffsetMHz = 0;
            caps.minMemoryClockOffsetMHz = -200;
            caps.maxMemoryClockOffsetMHz = 400;
            caps.defaultMemoryClockOffsetMHz = 0;

            unsigned int minMw = 0, maxMw = 0, enforcedMw = 0;
            const bool haveConstraints = r.deviceGetPowerManagementLimitConstraints &&
                r.deviceGetPowerManagementLimitConstraints(d, &minMw, &maxMw) == kNvmlSuccess && maxMw > 0;
            const bool haveEnforced = r.deviceGetEnforcedPowerLimit &&
                r.deviceGetEnforcedPowerLimit(d, &enforcedMw) == kNvmlSuccess && enforcedMw > 0;
            if (haveConstraints && haveEnforced && r.deviceSetPowerManagementLimit) {
                caps.canControlPowerLimit = true;
                // As bandas percentuais virao dos watts reais que este driver aceita,
                // nao da constante do Windows: 100% e o limite imposto pelo firmware.
                caps.minPowerLimitPercent = std::max(10, ValueToPercent(minMw, enforcedMw));
                caps.maxPowerLimitPercent = std::max(caps.minPowerLimitPercent + 1, ValueToPercent(maxMw, enforcedMw));
                caps.defaultPowerLimitPercent = 100;
            } else {
                caps.canControlPowerLimit = false;
                caps.minPowerLimitPercent = 60;
                caps.maxPowerLimitPercent = 110;
                caps.defaultPowerLimitPercent = 100;
            }

            ctx.canPstates = caps.canControlCoreClock;
            ctx.canPower = caps.canControlPowerLimit;
            caps.supported = caps.canControlCoreClock || caps.canControlMemoryClock || caps.canControlPowerLimit;
            if (caps.supported) {
                caps.limitationReason = "Driver NVIDIA via NVML ativo. Controles restritos aos limites seguros da GPU.";
            } else {
                caps.limitationReason = "NVML nao expoe controles de relogio nem de energia para esta GPU (WSL e "
                                        "drivers sem permissao devolvem so telemetria).";
            }
            return;
        }
        caps.supported = false;
        caps.limitationReason = r.ready
            ? "NVML carregado, mas sem handle NVIDIA para este adaptador. Telemetria sysfs preservada."
            : "NVML indisponivel no driver instalado. Telemetria sysfs preservada.";
        break;
    }
    case GpuVendor::Amd: {
        if (index < GpuPaths().size() && !GpuPaths()[index].cardDir.empty()) {
            const SysfsGpuPaths& p = GpuPaths()[index];
            caps.canControlPowerLimit = !p.powerCapFile.empty() && p.defaultPowerMicrowatts > 0;
            caps.minPowerLimitPercent = 70;
            caps.maxPowerLimitPercent = 115;
            caps.defaultPowerLimitPercent = 100;
            caps.canControlCoreClock = false;
            caps.canControlMemoryClock = false;
            caps.supported = caps.canControlPowerLimit;
            if (caps.supported) {
                caps.limitationReason = "amdgpu expoe limite de energia por sysfs; offsets de relogio nao tem "
                                        "interface publica (debugfs pp_dpm nao e suportada).";
            } else {
                caps.limitationReason = "Driver AMD ADLX nao existe em Linux e o sysfs amdgpu deste kernel nao expoe "
                                        "power1_cap. Telemetria preservada.";
            }
            return;
        }
        caps.supported = false;
        caps.limitationReason = "Nenhum adaptador AMD amdgpu ligado via sysfs DRM neste ambiente.";
        break;
    }
    case GpuVendor::Intel: {
        if (index < GpuPaths().size() && !GpuPaths()[index].cardDir.empty()) {
            const SysfsGpuPaths& p = GpuPaths()[index];
            unsigned long long rp0 = 0, rp1 = 0, curMin = 0, curMax = 0;
            const bool haveRp0 = !p.gtDir.empty() && ReadU64File(p.gtDir + "/gt_RP0_freq_mhz", rp0) && rp0 > 0;
            const bool haveRp1 = !p.gtDir.empty() && ReadU64File(p.gtDir + "/gt_RP1_freq_mhz", rp1) && rp1 > 0;
            const bool haveLimits = !p.gtDir.empty() &&
                ReadU64File(p.gtDir + "/gt_min_freq_mhz", curMin) &&
                ReadU64File(p.gtDir + "/gt_max_freq_mhz", curMax);

            caps.canControlCoreClock = haveRp0 && haveLimits;
            if (caps.canControlCoreClock) {
                // O headroom real e rp0 (maximo do silicio) menos rp1 (maximo normal);
                // abaixo, o minimo do silicio grafico.
                const unsigned long long ceiling = rp0;
                const unsigned long long floorMHz = haveRp1 ? rp1 : curMax;
                caps.maxCoreClockOffsetMHz = static_cast<int>(ceiling > floorMHz ? std::min<unsigned long long>(ceiling - floorMHz, 400ULL) : 0ULL);
                caps.minCoreClockOffsetMHz = -(caps.maxCoreClockOffsetMHz > 0 ? caps.maxCoreClockOffsetMHz : 200);
                if (caps.maxCoreClockOffsetMHz == 0) caps.canControlCoreClock = false;
            } else {
                caps.minCoreClockOffsetMHz = -150;
                caps.maxCoreClockOffsetMHz = 200;
            }
            caps.defaultCoreClockOffsetMHz = 0;
            caps.canControlMemoryClock = false;   // o i915 nao expoe relogio de memoria por GT
            caps.canControlPowerLimit = !p.powerCapFile.empty() && p.defaultPowerMicrowatts > 0;
            caps.minPowerLimitPercent = 70;
            caps.maxPowerLimitPercent = 120;
            caps.defaultPowerLimitPercent = 100;

            caps.supported = caps.canControlCoreClock || caps.canControlPowerLimit;
            if (caps.supported) {
                caps.limitationReason = "i915/xe expoe gt_min/gt_max_freq_mhz e power1_cap; escrita pede root.";
            } else {
                caps.limitationReason = "Sem interface sysfs de frequencia GT para este Intel (driver antigo ou "
                                        "virtualizado). Telemetria preservada.";
            }
            return;
        }
        caps.supported = false;
        caps.limitationReason = "Nenhum adaptador Intel i915/xe ligado via sysfs DRM neste ambiente.";
        break;
    }
    default:
        caps.supported = false;
        caps.limitationReason = "Fabricante de GPU nao suporta controle direto de clock por API publica.";
        break;
    }

    caps.minCoreClockOffsetMHz = -200;
    caps.maxCoreClockOffsetMHz = 200;
    caps.minMemoryClockOffsetMHz = -200;
    caps.maxMemoryClockOffsetMHz = 400;
}

// ============================================================================
// Aplicacao NVIDIA (NVML: relogios presos a valores da tabela do VBIOS)
// ============================================================================
bool GpuControlManager::ApplyNvidia(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg) {
    NvmlRuntime& r = Nvml();
    if (gpuIndex >= m_hwContexts.size() || !m_hwContexts[gpuIndex].nativeHandle || !r.ready) {
        outMsg = "Handle físico da GPU NVIDIA indisponível.";
        return false;
    }
    void* d = m_hwContexts[gpuIndex].nativeHandle;
    const GpuControlCapabilities& caps = m_gpus[gpuIndex];

    std::string details;
    bool ok = true;

    // 1. Relogio do nucleo: offset 0 devolve ao padrao do driver; qualquer outro
    //    valor prende P0 ao relogio suportado mais proximo de (maximo + offset).
    if (coreOffsetMHz != 0) {
        if (!caps.canControlCoreClock || !r.deviceSetGpuLockedClocks) {
            details += "Offset de core clock nao aplicavel por NVML neste driver; nada foi preso. ";
        } else {
            unsigned int maxGfx = 0;
            if (!r.deviceGetMaxClockInfo ||
                r.deviceGetMaxClockInfo(d, kNvmlClockGraphics, &maxGfx) != kNvmlSuccess || maxGfx == 0) {
                details += "NVML nao devolveu o relogio maximo do grafico. ";
                ok = false;
            } else {
                unsigned int maxMem = 0;
                if (r.deviceGetMaxClockInfo) r.deviceGetMaxClockInfo(d, kNvmlClockMemory, &maxMem);
                std::vector<unsigned int> gfxList;
                ReadSupportedClocks(d, true, maxMem, gfxList);

                const unsigned int asked = static_cast<unsigned int>(
                    std::max<long>(1, static_cast<long>(maxGfx) + coreOffsetMHz));
                unsigned int chosen = asked;
                if (!gfxList.empty()) PickSupported(gfxList, asked, chosen);
                const int rc = r.deviceSetGpuLockedClocks(d, chosen, chosen);
                if (rc == kNvmlSuccess) {
                    details += "Core clock NVIDIA preso em " + std::to_string(chosen) + " MHz (pedido " +
                               std::to_string(coreOffsetMHz) + " MHz sobre " + std::to_string(maxGfx) +
                               " MHz de maximo" + (chosen == asked ? "" : ", ajuste ao degrau do VBIOS") + "). ";
                } else {
                    details += "Driver NVIDIA rejeitou offset de frequencia (cod: " + std::to_string(rc) + "). ";
                    ok = false;
                }
            }
        }
    } else if (r.deviceResetGpuLockedClocks) {
        const int rc = r.deviceResetGpuLockedClocks(d);
        if (rc == kNvmlSuccess) {
            details += "Core clock NVIDIA devolvido ao padrao do driver. ";
        } else {
            details += "NVML nao pode devolver os relogios do nucleo ao padrao (cod: " + std::to_string(rc) + "). ";
            ok = false;
        }
    }

    // 2. Relogio de memoria, mesma logica com a tabela de memorias do VBIOS.
    if (memOffsetMHz != 0) {
        if (!caps.canControlMemoryClock || !r.deviceSetMemoryLockedClocks) {
            details += "Offset de memoria nao aplicavel por NVML neste driver; nada foi preso. ";
        } else {
            unsigned int maxMem = 0;
            if (!r.deviceGetMaxClockInfo ||
                r.deviceGetMaxClockInfo(d, kNvmlClockMemory, &maxMem) != kNvmlSuccess || maxMem == 0) {
                details += "NVML nao devolveu o relogio maximo de memoria. ";
                ok = false;
            } else {
                std::vector<unsigned int> memList;
                ReadSupportedClocks(d, false, 0, memList);
                unsigned int chosen = maxMem;
                if (!memList.empty()) PickSupported(memList, static_cast<unsigned int>(
                    static_cast<long>(maxMem) + memOffsetMHz), chosen);
                const int rc = r.deviceSetMemoryLockedClocks(d, chosen, chosen);
                if (rc == kNvmlSuccess) {
                    details += "Memory clock NVIDIA preso em " + std::to_string(chosen) + " MHz. ";
                } else {
                    details += "Driver NVIDIA rejeitou offset de memoria (cod: " + std::to_string(rc) + "). ";
                    ok = false;
                }
            }
        }
    } else if (r.deviceResetMemoryLockedClocks) {
        const int rc = r.deviceResetMemoryLockedClocks(d);
        if (rc == kNvmlSuccess) {
            details += "Memory clock NVIDIA devolvido ao padrao do driver. ";
        }
    }

    // 3. Limite de energia em watts, derivado do percentaje sobre o limite imposto.
    if (r.deviceSetPowerManagementLimit && caps.canControlPowerLimit) {
        unsigned int minMw = 0, maxMw = 0, enforcedMw = 0;
        if (r.deviceGetPowerManagementLimitConstraints &&
            r.deviceGetPowerManagementLimitConstraints(d, &minMw, &maxMw) == kNvmlSuccess &&
            r.deviceGetEnforcedPowerLimit &&
            r.deviceGetEnforcedPowerLimit(d, &enforcedMw) == kNvmlSuccess && enforcedMw > 0) {
            const unsigned long long wantMw = PercentToValue(enforcedMw, powerLimitPercent);
            const unsigned int appliedMw = static_cast<unsigned int>(std::min<unsigned long long>(
                std::max<unsigned long long>(wantMw, minMw), maxMw));
            const int rc = r.deviceSetPowerManagementLimit(d, appliedMw);
            if (rc == kNvmlSuccess) {
                details += "Power Limit NVIDIA ajustado para " + std::to_string(powerLimitPercent) + "% (" +
                           std::to_string(appliedMw / 1000ULL) + " W). ";
            } else {
                details += "Power limit " + std::to_string(powerLimitPercent) +
                           "% rejeitado pelo driver (cod: " + std::to_string(rc) + "). ";
                ok = false;
            }
        } else {
            details += "NVML nao expoe as restricoes de energia desta GPU; nada foi escrito. ";
        }
    }

    outMsg = details.empty() ? "Nada a aplicar no NVIDIA (valores ja no padrao do driver)." : details;
    return ok;
}

// ============================================================================
// Aplicacao AMD (sysfs amdgpu)
// ============================================================================
bool GpuControlManager::ApplyAmd(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg) {
    if (gpuIndex >= GpuPaths().size() || GpuPaths()[gpuIndex].cardDir.empty()) {
        outMsg = "Índice de adaptador AMD indisponível.";
        return false;
    }
    SysfsGpuPaths& p = GpuPaths()[gpuIndex];
    const GpuControlCapabilities& caps = m_gpus[gpuIndex];

    std::string details;
    bool ok = true;

    if (coreOffsetMHz != 0 || memOffsetMHz != 0) {
        details += "Offsets de clock AMD nao aplicaveis por sysfs (o amdgpu nao expoe interface publica de "
                   "offsets); nada foi escrito. ";
    }

    if (powerLimitPercent != 100 || caps.canControlPowerLimit) {
        if (p.powerCapFile.empty() || p.defaultPowerMicrowatts == 0) {
            details += "Interface sysfs de power limit indisponivel; nada foi escrito no driver. ";
        } else {
            const unsigned long long want = PercentToValue(p.defaultPowerMicrowatts, powerLimitPercent);
            unsigned long long back = 0;
            bool wrote = false;
            {
                std::ofstream f(p.powerCapFile, std::ios::trunc);
                if (f) { f << want; f.flush(); wrote = static_cast<bool>(f); }
            }
            const bool readBack = ReadU64File(p.powerCapFile, back);
            if (wrote && readBack && back == want) {
                details += "Power Limit AMD ajustado para " + std::to_string(powerLimitPercent) + "% (" +
                           std::to_string(want / 1000000ULL) + " W). ";
            } else if (readBack && back != p.defaultPowerMicrowatts) {
                // O kernel clampou para a faixa que a placa aguenta: declara-se o valor real.
                details += "Power Limit AMD pedido em " + std::to_string(powerLimitPercent) +
                           "%, o kernel aplicou " + std::to_string(back / 1000000ULL) + " W. ";
            } else {
                details += "Kernel rejeitou o power limit AMD (permissao ou faixa); nada confirmado por leitura. ";
                ok = false;
            }
        }
    }

    outMsg = details.empty() ? "Nada a aplicar no AMD (valores ja no padrao do driver)." : details;
    return ok;
}

// ============================================================================
// Aplicacao Intel (i915/xe gt_min_freq_mhz / gt_max_freq_mhz)
// ============================================================================
bool GpuControlManager::ApplyIntel(size_t gpuIndex, int coreOffsetMHz, int memOffsetMHz, int powerLimitPercent, std::string& outMsg) {
    if (gpuIndex >= GpuPaths().size() || GpuPaths()[gpuIndex].gtDir.empty()) {
        outMsg = "Handle de dispositivo Intel indisponível.";
        return false;
    }
    SysfsGpuPaths& p = GpuPaths()[gpuIndex];

    std::string details;
    bool ok = true;

    if (coreOffsetMHz != 0) {
        unsigned long long rp0 = 0;
        if (!ReadU64File(p.gtDir + "/gt_RP0_freq_mhz", rp0) || rp0 == 0) {
            details += "Interface sysfs de offset de frequencia indisponivel; nada foi escrito. ";
        } else {
            const unsigned long long target = static_cast<unsigned long long>(
                std::max<long>(1, static_cast<long>(rp0) + coreOffsetMHz));
            // gt_min nunca pode ficar acima de gt_max ou o kernel recusa a dupla:
            // para underclock o minimo segue o teto novo.
            const unsigned long long newMin = p.hasOrigGt ? std::min(p.origGtMinMHz, target) : 0ULL;
            const bool wroteMax = WriteU64File(p.gtDir + "/gt_max_freq_mhz", target);
            const bool wroteMin = WriteU64File(p.gtDir + "/gt_min_freq_mhz", newMin);
            if (wroteMax && wroteMin) {
                details += "Offset de frequencia Intel aplicado (" + std::to_string(coreOffsetMHz) +
                           " MHz sobre RP0 " + std::to_string(rp0) + " MHz, teto agora " +
                           std::to_string(target) + " MHz). ";
            } else {
                details += "Driver Intel rejeitou offset de frequencia (sysfs pede root, ou a faixa e invalida). ";
                ok = false;
            }
        }
    } else if (p.hasOrigGt) {
        // Estado EXATO: devolve-se o par min/max lido antes de qualquer escrita, por esta
        // ordem (max primeiro deixa o min original de novo dentro da faixa).
        const bool wroteMax = WriteU64File(p.gtDir + "/gt_max_freq_mhz", p.origGtMaxMHz);
        const bool wroteMin = WriteU64File(p.gtDir + "/gt_min_freq_mhz", p.origGtMinMHz);
        if (wroteMin && wroteMax) {
            details += "Frequencias GT Intel restauradas ao estado original (" + std::to_string(p.origGtMinMHz) +
                       "-" + std::to_string(p.origGtMaxMHz) + " MHz). ";
        } else {
            details += "Nao foi possivel restaurar as frequencias GT originais (permissao). ";
            ok = false;
        }
    } else {
        details += "Offset zero Intel sem estado original capturado: nada escrito. ";
    }

    if (memOffsetMHz != 0) {
        details += "Offset de memoria Intel nao aplicavel (o i915 nao expoe relogio de memoria por GT). ";
    }

    if (powerLimitPercent != 100 || !p.powerCapFile.empty()) {
        if (p.powerCapFile.empty() || p.defaultPowerMicrowatts == 0) {
            details += "Interface sysfs de power limit indisponivel; nada foi escrito. ";
        } else {
            const unsigned long long want = PercentToValue(p.defaultPowerMicrowatts, powerLimitPercent);
            unsigned long long back = 0;
            bool wrote = false;
            {
                std::ofstream f(p.powerCapFile, std::ios::trunc);
                if (f) { f << want; f.flush(); wrote = static_cast<bool>(f); }
            }
            if (wrote && ReadU64File(p.powerCapFile, back) && back == want) {
                details += "Power Limit Intel ajustado (" + std::to_string(powerLimitPercent) + "%, " +
                           std::to_string(want / 1000000ULL) + " W). ";
            } else {
                details += "Driver Intel rejeitou power limit (permissao ou faixa). ";
                ok = false;
            }
        }
    }

    outMsg = details.empty() ? "Nada a aplicar no Intel (valores ja no padrao do driver)." : details;
    return ok;
}

// ============================================================================
// Telemetria
// ============================================================================
void GpuControlManager::UpdateNvidiaTelemetry(size_t gpuIndex) {
    NvmlRuntime& r = Nvml();
    if (gpuIndex >= m_statuses.size() || gpuIndex >= m_hwContexts.size()) return;
    GpuControlStatus& st = m_statuses[gpuIndex];

    if (r.ready && m_hwContexts[gpuIndex].nativeHandle) {
        void* d = m_hwContexts[gpuIndex].nativeHandle;

        unsigned int clock = 0;
        if (r.deviceGetClockInfo && r.deviceGetClockInfo(d, kNvmlClockGraphics, &clock) == kNvmlSuccess) {
            st.measuredCoreClockMHz = static_cast<int>(clock);
        }
        clock = 0;
        if (r.deviceGetClockInfo && r.deviceGetClockInfo(d, kNvmlClockMemory, &clock) == kNvmlSuccess) {
            st.measuredMemoryClockMHz = static_cast<int>(clock);
        }
        unsigned int temp = 0;
        if (r.deviceGetTemperature && r.deviceGetTemperature(d, kNvmlTempGpu, &temp) == kNvmlSuccess) {
            st.measuredTemperatureC = static_cast<double>(temp);
        }
        NvmlUtilization util{};
        if (r.deviceGetUtilizationRates && r.deviceGetUtilizationRates(d, &util) == kNvmlSuccess) {
            st.measuredUsagePercent = static_cast<int>(util.gpu);
        }
        unsigned int mw = 0;
        if (r.deviceGetPowerUsage && r.deviceGetPowerUsage(d, &mw) == kNvmlSuccess) {
            st.measuredPowerWatts = static_cast<int>(mw / 1000ULL);
        }
    }

    // Onde o NVML nao devolve o campo (medido em WSL2: temperatura e consumo ficam fora),
    // o hwmon do proprio driver nvidia DRM tem o numero. So se preenche o que falta, para
    // o NVML continuar a ganhar quando os dois concordam ou divergem.
    if (gpuIndex < GpuPaths().size()) {
        const SysfsGpuPaths& p = GpuPaths()[gpuIndex];
        unsigned long long v = 0;
        if (!p.hwmonDir.empty()) {
            if (st.measuredTemperatureC <= 0.0 && ReadU64File(p.hwmonDir + "/temp1_input", v) && v < 130000ULL) {
                st.measuredTemperatureC = static_cast<double>(v) / 1000.0;
            }
            if (st.measuredPowerWatts == 0 && ReadU64File(p.hwmonDir + "/power1_input", v)) {
                st.measuredPowerWatts = static_cast<int>(v / 1000000ULL);
            }
            if (st.measuredCoreClockMHz == 0 && ReadU64File(p.hwmonDir + "/freq1_input", v)) {
                st.measuredCoreClockMHz = static_cast<int>(v);
            }
        }
    }
}

void GpuControlManager::UpdateAmdTelemetry(size_t gpuIndex) {
    if (gpuIndex >= m_statuses.size() || gpuIndex >= GpuPaths().size()) return;
    const SysfsGpuPaths& p = GpuPaths()[gpuIndex];
    GpuControlStatus& st = m_statuses[gpuIndex];

    unsigned long long v = 0;
    if (!p.coreFreqFile.empty() && ReadU64File(p.coreFreqFile, v)) st.measuredCoreClockMHz = static_cast<int>(v);

    if (!p.deviceDir.empty() && ReadU64File(p.deviceDir + "/gpu_busy_percent", v)) {
        st.measuredUsagePercent = static_cast<int>(v);
    }
    if (!p.hwmonDir.empty()) {
        if (ReadU64File(p.hwmonDir + "/temp1_input", v) && v < 130000ULL) {
            st.measuredTemperatureC = static_cast<double>(v) / 1000.0;
        }
        if (ReadU64File(p.hwmonDir + "/power1_input", v)) {
            st.measuredPowerWatts = static_cast<int>(v / 1000000ULL);
        }
        if (st.measuredCoreClockMHz == 0 && ReadU64File(p.hwmonDir + "/freq1_input", v)) {
            st.measuredCoreClockMHz = static_cast<int>(v);
        }
    }
}

void GpuControlManager::UpdateIntelTelemetry(size_t gpuIndex) {
    if (gpuIndex >= m_statuses.size() || gpuIndex >= GpuPaths().size()) return;
    const SysfsGpuPaths& p = GpuPaths()[gpuIndex];
    GpuControlStatus& st = m_statuses[gpuIndex];

    unsigned long long v = 0;
    if (!p.coreFreqFile.empty() && ReadU64File(p.coreFreqFile, v)) st.measuredCoreClockMHz = static_cast<int>(v);
    // O i915 nao publica percentagem de uso do motor grafico em sysfs (a unica via e o
    // perf_interface/debugfs, fora do ABI estavel). Dividir gt_cur por gt_max daria uma
    // proporcao de relogio, nao de uso, e o campo da UI diz "uso": fica em 0 e declarado
    // como indisponivel, como no ramo Windows quando o provider nao responde.
    if (!p.hwmonDir.empty()) {
        if (ReadU64File(p.hwmonDir + "/temp1_input", v) && v < 130000ULL) {
            st.measuredTemperatureC = static_cast<double>(v) / 1000.0;
        }
        if (ReadU64File(p.hwmonDir + "/power1_input", v)) {
            st.measuredPowerWatts = static_cast<int>(v / 1000000ULL);
        }
    }
}

// ============================================================================
// Perfis e configuracoes customizadas (mesma core do Windows)
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

    if (m_hasMock) {
        pStatus->appliedPowerLimitPercent = pStatus->requestedPowerLimitPercent;
        pStatus->appliedCoreClockOffsetMHz = pStatus->requestedCoreClockOffsetMHz;
        pStatus->appliedMemoryClockOffsetMHz = pStatus->requestedMemoryClockOffsetMHz;
        outMsg = "Perfil " + std::string(GpuPowerProfileToString(profile)) + " aplicado com sucesso.";
        return true;
    }

    std::string hardwareMsg;
    bool ok = false;
    if (caps.vendor == GpuVendor::Nvidia) {
        ok = ApplyNvidia(gpuIndex, pStatus->requestedCoreClockOffsetMHz, pStatus->requestedMemoryClockOffsetMHz,
                         pStatus->requestedPowerLimitPercent, hardwareMsg);
    } else if (caps.vendor == GpuVendor::Amd) {
        ok = ApplyAmd(gpuIndex, pStatus->requestedCoreClockOffsetMHz, pStatus->requestedMemoryClockOffsetMHz,
                      pStatus->requestedPowerLimitPercent, hardwareMsg);
    } else if (caps.vendor == GpuVendor::Intel) {
        ok = ApplyIntel(gpuIndex, pStatus->requestedCoreClockOffsetMHz, pStatus->requestedMemoryClockOffsetMHz,
                        pStatus->requestedPowerLimitPercent, hardwareMsg);
    }

    if (!ok) {
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

} // namespace opitcore
#endif // !_WIN32 (gemelo de GpuControlManager.cpp para NVML + sysfs)
